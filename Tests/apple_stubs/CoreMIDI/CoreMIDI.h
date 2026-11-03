/* Minimal stand-in for Apple's CoreMIDI header, just enough to compile
 * Sources/standalone_bridge.c on Windows for Tests/macos_bridge_logic.c.
 * The functions are implemented by that test, which records the read
 * callback so it can feed MIDI bytes to the bridge. Not used on macOS. */

#ifndef DELAY_LAMA_COREMIDI_STUB_H
#define DELAY_LAMA_COREMIDI_STUB_H

#include <stddef.h>
#include <stdint.h>

typedef uint8_t Byte;
typedef uint16_t UInt16;
typedef uint32_t UInt32;
typedef int32_t OSStatus;
typedef unsigned long ItemCount;
typedef uint64_t MIDITimeStamp;
typedef UInt32 MIDIObjectRef;
typedef MIDIObjectRef MIDIClientRef;
typedef MIDIObjectRef MIDIPortRef;
typedef MIDIObjectRef MIDIEndpointRef;
typedef const char *CFStringRef;

#define CFSTR(text) ((CFStringRef)(text))

enum { noErr = 0 };

#pragma pack(push, 4)
typedef struct MIDIPacket {
    MIDITimeStamp timeStamp;
    UInt16 length;
    Byte data[256];
} MIDIPacket;

typedef struct MIDIPacketList {
    UInt32 numPackets;
    MIDIPacket packet[1];
} MIDIPacketList;
#pragma pack(pop)

typedef void (*MIDIReadProc)(const MIDIPacketList *packets, void *read_refcon,
                             void *source_refcon);
typedef void (*MIDINotifyProc)(const void *message, void *refcon);

/* Packets follow each other, 4-byte aligned as on arm64. */
static inline MIDIPacket *MIDIPacketNext(const MIDIPacket *packet) {
    uintptr_t next = (uintptr_t)&packet->data[packet->length];
    return (MIDIPacket *)((next + 3u) & ~(uintptr_t)3u);
}

OSStatus MIDIClientCreate(CFStringRef name, MIDINotifyProc notify, void *notify_refcon,
                          MIDIClientRef *out_client);
OSStatus MIDIInputPortCreate(MIDIClientRef client, CFStringRef name, MIDIReadProc read,
                             void *refcon, MIDIPortRef *out_port);
ItemCount MIDIGetNumberOfSources(void);
MIDIEndpointRef MIDIGetSource(ItemCount index);
OSStatus MIDIPortConnectSource(MIDIPortRef port, MIDIEndpointRef source, void *connection_refcon);
OSStatus MIDIPortDispose(MIDIPortRef port);
OSStatus MIDIClientDispose(MIDIClientRef client);

#endif
