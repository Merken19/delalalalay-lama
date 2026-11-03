/* Runs the macOS bridge (Sources/standalone_bridge.c) on Windows, compiled
 * against the stub Apple headers in Tests/apple_stubs. It covers the
 * platform-independent logic (the shared bridge checks) plus the CoreMIDI
 * byte parser, which only the macOS bridge has. The real CoreMIDI build is
 * covered by Tests/run_macos_tests.sh on macOS. */

#include "bridge_checks.h"

#include <CoreMIDI/CoreMIDI.h>

#include <stdlib.h>

int stub_lock_errors = 0;
static MIDIReadProc stub_read = NULL;
static void *stub_refcon = NULL;
static int stub_ports_disposed = 0;
static int stub_clients_disposed = 0;

OSStatus MIDIClientCreate(CFStringRef name, MIDINotifyProc notify, void *notify_refcon,
                          MIDIClientRef *out_client) {
    (void)name;
    (void)notify;
    (void)notify_refcon;
    *out_client = 1;
    return noErr;
}

OSStatus MIDIInputPortCreate(MIDIClientRef client, CFStringRef name, MIDIReadProc read,
                             void *refcon, MIDIPortRef *out_port) {
    (void)client;
    (void)name;
    stub_read = read;
    stub_refcon = refcon;
    *out_port = 2;
    return noErr;
}

/* Three sources, one of which has gone away: two get connected. */
ItemCount MIDIGetNumberOfSources(void) { return 3; }

MIDIEndpointRef MIDIGetSource(ItemCount index) {
    return index == 1 ? 0 : (MIDIEndpointRef)(100 + index);
}

OSStatus MIDIPortConnectSource(MIDIPortRef port, MIDIEndpointRef source, void *connection_refcon) {
    (void)port;
    (void)source;
    (void)connection_refcon;
    return noErr;
}

OSStatus MIDIPortDispose(MIDIPortRef port) {
    (void)port;
    stub_ports_disposed++;
    return noErr;
}

OSStatus MIDIClientDispose(MIDIClientRef client) {
    (void)client;
    stub_clients_disposed++;
    return noErr;
}

/* Delivers each message as one packet of a single packet list, the way
 * CoreMIDI calls the bridge's read callback. */
static void send_packets(const uint8_t *const *messages, const uint16_t *lengths, int count) {
    static union {
        MIDIPacketList list;
        uint8_t raw[4096];
    } buffer;
    memset(&buffer, 0, sizeof(buffer));
    buffer.list.numPackets = (UInt32)count;
    MIDIPacket *packet = &buffer.list.packet[0];
    for (int index = 0; index < count; ++index) {
        packet->length = lengths[index];
        memcpy(packet->data, messages[index], lengths[index]);
        packet = MIDIPacketNext(packet);
    }
    stub_read(&buffer.list, stub_refcon, NULL);
}

static void send(const uint8_t *bytes, uint16_t length) { send_packets(&bytes, &length, 1); }

#define SEND(...)                                                                                  \
    do {                                                                                           \
        static const uint8_t message_[] = {__VA_ARGS__};                                           \
        send(message_, (uint16_t)sizeof(message_));                                                \
    } while (0)

static void render(DelayLamaHandle *handle, int blocks) {
    float left[512];
    float right[512];
    for (int block = 0; block < blocks; ++block)
        dl_process(handle, left, right, 512);
}

static int failures = 0;

static void check(int condition, const char *what) {
    if (!condition) {
        fprintf(stderr, "macos_bridge_logic: FAIL %s\n", what);
        failures++;
    }
}

int main(void) {
    DelayLamaHandle *handle = dl_create(48000.0f);
    if (!handle || !stub_read) {
        fprintf(stderr, "macos_bridge_logic: dl_create failed or no MIDI port was opened\n");
        return 1;
    }
    check(dl_midi_source_count(handle) == 2, "connects every source that is still present");

    if (run_bridge_checks(handle, NULL) != 0) {
        dl_destroy(handle);
        return 1;
    }
    dl_set_glide(handle, 0.0f);

    SEND(0x90, 0x3C, 0x64);
    check(dl_is_active(handle), "note on");
    render(handle, 4);
    check(fabsf(dl_get_pitch(handle) - 1.0f) < 0.01f, "note on plays C4");

    SEND(0x80, 0x3C, 0x00);
    check(!dl_is_active(handle), "note off");

    /* Running status applies within a packet (CoreMIDI never carries it
     * over to the next one). */
    SEND(0x90, 0x3C, 0x64, 0x3E, 0x64);
    SEND(0x80, 0x3E, 0x00);
    check(dl_is_active(handle), "running status note on, then fall back to the held note");
    SEND(0x90, 0x3E, 0x64);
    SEND(0x80, 0x3E, 0x00, 0x3C, 0x00);
    check(!dl_is_active(handle), "running status note off ends the voice");

    SEND(0x3C, 0x64);
    check(!dl_is_active(handle), "data bytes without a status byte are ignored");

    /* A SysEx message used to hang the CoreMIDI thread: its data bytes have
     * no running status and the parser never moved past them. */
    SEND(0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7);
    check(!dl_is_active(handle), "SysEx is ignored");
    SEND(0xF0, 0x01, 0x02, 0xF7, 0x90, 0x40, 0x64);
    check(dl_is_active(handle), "a note after SysEx in the same packet plays");
    SEND(0x80, 0x40, 0x00);
    check(!dl_is_active(handle), "note off after SysEx");

    /* Realtime bytes such as MIDI clock keep the running status. */
    SEND(0x90, 0x3C, 0x64, 0xF8, 0x3E, 0x64);
    SEND(0x80, 0x3E, 0x00);
    check(dl_is_active(handle), "running status survives a clock byte");
    SEND(0xB0, 0x7B, 0x00);
    check(!dl_is_active(handle), "CC 123 releases every held note");

    SEND(0xC0, 0x05, 0x90, 0x3C, 0x64);
    check(dl_is_active(handle), "a one-byte program change is skipped correctly");
    SEND(0xE0, 0x7F, 0x7F);
    render(handle, 20);
    check(dl_get_vowel(handle) > 0.9f, "pitch wheel up sets the brightest vowel");
    SEND(0xE0, 0x00, 0x00);
    render(handle, 20);
    check(dl_get_vowel(handle) < 0.1f, "pitch wheel down sets the darkest vowel");
    SEND(0x80, 0x3C, 0x00);
    check(!dl_is_active(handle), "note off after the pitch wheel");

    {
        static const uint8_t first[] = {0x90, 0x3C, 0x64};
        static const uint8_t second[] = {0x90, 0x3E, 0x64};
        const uint8_t *const messages[] = {first, second};
        const uint16_t lengths[] = {3, 3};
        send_packets(messages, lengths, 2);
        SEND(0x80, 0x3E, 0x00);
        check(dl_is_active(handle), "every packet of a packet list is read");
        SEND(0x80, 0x3C, 0x00);
        check(!dl_is_active(handle), "note off of the first packet's note");
    }

    SEND(0x90, 0x3C);
    check(!dl_is_active(handle), "a truncated message is dropped");

    dl_xy_begin(handle, 0.5f, 0.5f);
    SEND(0xB0, 0x78, 0x00);
    check(!dl_is_active(handle), "CC 120 also silences the flag");
    dl_xy_end(handle);

    check(stub_lock_errors == 0, "the bridge never takes its lock twice");
    dl_destroy(handle);
    check(stub_ports_disposed == 1 && stub_clients_disposed == 1,
          "destroy disposes the MIDI port and client");

    if (failures) {
        fprintf(stderr, "macos_bridge_logic: %d check(s) failed\n", failures);
        return 1;
    }
    printf("macos_bridge_logic: PASS\n");
    return 0;
}
