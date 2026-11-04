#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

#include "../DSP/synth.h"
#include "../Sources/standalone_bridge.h"
#include "voice_bridge.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define XY_SENTINEL_NOTE 127
#define MAX_MIDI_INPUTS 32

struct DelayLamaHandle {
    MonkSynthEngine *synth;
    CRITICAL_SECTION lock;
    HMIDIIN midi_inputs[MAX_MIDI_INPUTS];
    int32_t midi_sources;
    uint8_t xy_active;
    uint8_t xy_snap;
    float xy_low_note;
    float xy_high_note;
    float xy_hz; /* last flag pitch */
    uint8_t held[128];
};

static float clamp01(float value) {
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

static float clamp_range(float value, float low, float high) {
    return value < low ? low : (value > high ? high : value);
}

static float xy_pitch_hz(const DelayLamaHandle *handle, float normalized) {
    float midi_note = handle->xy_low_note +
                      clamp01(normalized) * (handle->xy_high_note - handle->xy_low_note);
    if (handle->xy_snap)
        midi_note = floorf(midi_note + 0.5f);
    return 440.0f * powf(2.0f, (midi_note - 69.0f) / 12.0f);
}

static void locked_note_on(DelayLamaHandle *handle, uint8_t note, float velocity) {
    if (note == XY_SENTINEL_NOTE)
        return;
    handle->held[note] = 1;
    monk_synth_note_on(handle->synth, note, velocity);
}

static void locked_note_off(DelayLamaHandle *handle, uint8_t note) {
    if (note == XY_SENTINEL_NOTE)
        return;
    handle->held[note] = 0;
    monk_synth_note_off(handle->synth, note);
    /* A key released while the flag is held can leave the flag's placeholder
     * on top of the stack; the voice then follows the flag, not note 127. */
    if (handle->xy_active && monk_synth_top_note(handle->synth) == XY_SENTINEL_NOTE)
        monk_synth_set_pitch_hz(handle->synth, handle->xy_hz);
}

static void locked_all_notes_off(DelayLamaHandle *handle) {
    for (int note = 0; note < 127; ++note) {
        if (handle->held[note])
            locked_note_off(handle, (uint8_t)note);
    }
    if (handle->xy_active) {
        monk_synth_note_off(handle->synth, XY_SENTINEL_NOTE);
        handle->xy_active = 0;
    }
}

/* WinMM delivers short MIDI messages packed as status, data1, data2 in the
 * low 24 bits of dwParam1. The original bridge handles the same channel voice
 * messages, so SysEx and system realtime messages are intentionally ignored. */
static void handle_midi_message(DelayLamaHandle *handle, DWORD packed_message) {
    uint8_t status = (uint8_t)(packed_message & 0xffu);
    uint8_t data1 = (uint8_t)((packed_message >> 8) & 0xffu);
    uint8_t data2 = (uint8_t)((packed_message >> 16) & 0xffu);

    if ((status & 0x80u) == 0)
        return;

    EnterCriticalSection(&handle->lock);
    switch (status & 0xf0u) {
    case 0x80:
        locked_note_off(handle, data1 & 0x7fu);
        break;
    case 0x90:
        if (data2 == 0)
            locked_note_off(handle, data1 & 0x7fu);
        else
            locked_note_on(handle, data1 & 0x7fu, (float)data2 / 127.0f);
        break;
    case 0xb0:
        if (data1 == 123 || data1 == 120) {
            locked_all_notes_off(handle);
        } else {
            monk_synth_midi_cc(handle->synth, data1 & 0x7fu, (float)data2 / 127.0f);
        }
        break;
    case 0xe0: {
        int bend = ((int)(data2 & 0x7fu) << 7) | (data1 & 0x7fu);
        monk_synth_set_vowel(handle->synth, (float)bend / 16383.0f);
        break;
    }
    default:
        break;
    }
    LeaveCriticalSection(&handle->lock);
}

static void CALLBACK midi_read(HMIDIIN midi_input, UINT message, DWORD_PTR instance,
                               DWORD_PTR param1, DWORD_PTR param2) {
    (void)midi_input;
    (void)param2;
    DelayLamaHandle *handle = (DelayLamaHandle *)instance;
    if (!handle || message != MIM_DATA)
        return;
    handle_midi_message(handle, (DWORD)param1);
}

static void connect_midi_inputs(DelayLamaHandle *handle) {
    UINT device_count = midiInGetNumDevs();
    for (UINT device = 0; device < device_count && handle->midi_sources < MAX_MIDI_INPUTS;
         ++device) {
        HMIDIIN input = NULL;
        MMRESULT result = midiInOpen(&input, device, (DWORD_PTR)midi_read,
                                     (DWORD_PTR)handle, CALLBACK_FUNCTION);
        if (result != MMSYSERR_NOERROR || !input)
            continue;
        result = midiInStart(input);
        if (result != MMSYSERR_NOERROR) {
            midiInClose(input);
            continue;
        }
        handle->midi_inputs[handle->midi_sources++] = input;
    }
}

static void disconnect_midi_inputs(DelayLamaHandle *handle) {
    for (int32_t index = 0; index < handle->midi_sources; ++index) {
        HMIDIIN input = handle->midi_inputs[index];
        if (!input)
            continue;
        midiInStop(input);
        midiInReset(input);
        midiInClose(input);
        handle->midi_inputs[index] = NULL;
    }
    handle->midi_sources = 0;
}

static DelayLamaHandle *create_handle(float sample_rate, int open_midi_inputs) {
    DelayLamaHandle *handle = (DelayLamaHandle *)calloc(1, sizeof(*handle));
    if (!handle)
        return NULL;

    InitializeCriticalSection(&handle->lock);
    handle->synth = monk_synth_new(sample_rate);
    if (!handle->synth) {
        DeleteCriticalSection(&handle->lock);
        free(handle);
        return NULL;
    }

    monk_synth_set_volume(handle->synth, 0.28f);
    monk_synth_set_level(handle->synth, 0.75f);
    monk_synth_set_vibrato(handle->synth, 0.15f);
    monk_synth_set_delay_mix(handle->synth, 0.42f);
    handle->xy_low_note = 48.0f;
    handle->xy_high_note = 72.0f;

    if (open_midi_inputs)
        connect_midi_inputs(handle);
    return handle;
}

DelayLamaHandle *dl_create(float sample_rate) { return create_handle(sample_rate, 1); }

DelayLamaHandle *dl_create_without_midi(float sample_rate) {
    return create_handle(sample_rate, 0);
}

void dl_destroy(DelayLamaHandle *handle) {
    if (!handle)
        return;
    disconnect_midi_inputs(handle);
    EnterCriticalSection(&handle->lock);
    locked_all_notes_off(handle);
    LeaveCriticalSection(&handle->lock);
    monk_synth_free(handle->synth);
    DeleteCriticalSection(&handle->lock);
    free(handle);
}

void dl_process(DelayLamaHandle *handle, float *left, float *right, uint32_t frames) {
    if (!handle || !left || !right)
        return;
    EnterCriticalSection(&handle->lock);
    monk_synth_process(handle->synth, left, right, frames);
    LeaveCriticalSection(&handle->lock);
}

void dl_note_on(DelayLamaHandle *handle, uint8_t note, float velocity) {
    if (!handle)
        return;
    EnterCriticalSection(&handle->lock);
    locked_note_on(handle, note & 0x7fu, velocity);
    LeaveCriticalSection(&handle->lock);
}

void dl_note_off(DelayLamaHandle *handle, uint8_t note) {
    if (!handle)
        return;
    EnterCriticalSection(&handle->lock);
    locked_note_off(handle, note & 0x7fu);
    LeaveCriticalSection(&handle->lock);
}

void dl_all_notes_off(DelayLamaHandle *handle) {
    if (!handle)
        return;
    EnterCriticalSection(&handle->lock);
    locked_all_notes_off(handle);
    LeaveCriticalSection(&handle->lock);
}

void dl_xy_begin(DelayLamaHandle *handle, float pitch, float vowel) {
    if (!handle)
        return;
    EnterCriticalSection(&handle->lock);
    handle->xy_hz = xy_pitch_hz(handle, pitch);
    if (!handle->xy_active) {
        /* The placeholder note only holds the voice. Sounding its own pitch
         * would start an idle voice at 12.5 kHz and glide down to the flag
         * (a laser-like sweep on every press). */
        monk_synth_note_on_hz(handle->synth, XY_SENTINEL_NOTE, handle->xy_hz, 1.0f);
        handle->xy_active = 1;
    }
    monk_synth_set_vowel(handle->synth, clamp01(vowel));
    monk_synth_set_pitch_hz(handle->synth, handle->xy_hz);
    LeaveCriticalSection(&handle->lock);
}

void dl_xy_move(DelayLamaHandle *handle, float pitch, float vowel) {
    if (!handle)
        return;
    EnterCriticalSection(&handle->lock);
    handle->xy_hz = xy_pitch_hz(handle, pitch);
    monk_synth_set_vowel(handle->synth, clamp01(vowel));
    monk_synth_set_pitch_hz(handle->synth, handle->xy_hz);
    LeaveCriticalSection(&handle->lock);
}

void dl_xy_end(DelayLamaHandle *handle) {
    if (!handle)
        return;
    EnterCriticalSection(&handle->lock);
    if (handle->xy_active) {
        monk_synth_note_off(handle->synth, XY_SENTINEL_NOTE);
        handle->xy_active = 0;
        monk_synth_restore_note_stack(handle->synth);
    }
    LeaveCriticalSection(&handle->lock);
}

#define SETTER(name, call)                                                        \
    void name(DelayLamaHandle *handle, float value) {                              \
        if (!handle)                                                               \
            return;                                                               \
        EnterCriticalSection(&handle->lock);                                       \
        call(handle->synth, clamp01(value));                                       \
        LeaveCriticalSection(&handle->lock);                                       \
    }

SETTER(dl_set_glide, monk_synth_set_glide)
SETTER(dl_set_voice, monk_synth_set_voice)
SETTER(dl_set_delay, monk_synth_set_delay_mix)
SETTER(dl_set_vibrato, monk_synth_set_vibrato)
SETTER(dl_set_volume, monk_synth_set_volume)
SETTER(dl_set_vibrato_rate, monk_synth_set_vibrato_rate)
SETTER(dl_set_breath, monk_synth_set_aspiration)
SETTER(dl_set_delay_time, monk_synth_set_delay_rate)
SETTER(dl_set_delay_feedback, monk_synth_set_delay_feedback)
SETTER(dl_set_level, monk_synth_set_level)
SETTER(dl_set_vowel, monk_synth_set_vowel)

void dl_set_vibrato_seed(DelayLamaHandle *handle, uint32_t seed) {
    if (!handle)
        return;
    EnterCriticalSection(&handle->lock);
    monk_synth_set_vibrato_seed(handle->synth, seed);
    LeaveCriticalSection(&handle->lock);
}

void dl_set_formant_bandwidth(DelayLamaHandle *handle, float scale) {
    if (!handle)
        return;
    EnterCriticalSection(&handle->lock);
    monk_synth_set_formant_bandwidth(handle->synth, scale);
    LeaveCriticalSection(&handle->lock);
}

void dl_voice_note_on(DelayLamaHandle *handle, uint8_t note) {
    if (!handle || note >= XY_SENTINEL_NOTE)
        return;
    EnterCriticalSection(&handle->lock);
    /* One note per handle: sound the new note first (a sounding voice keeps
     * sounding and glides), then drop every other note from the stack; the
     * new note stays on top, so nothing falls back. */
    locked_note_on(handle, note, 1.0f);
    for (int other = 0; other < XY_SENTINEL_NOTE; ++other) {
        if (handle->held[other] && other != note)
            locked_note_off(handle, (uint8_t)other);
    }
    LeaveCriticalSection(&handle->lock);
}

void dl_voice_note_off(DelayLamaHandle *handle) {
    if (!handle)
        return;
    EnterCriticalSection(&handle->lock);
    locked_all_notes_off(handle);
    LeaveCriticalSection(&handle->lock);
}

void dl_set_pitch_bend(DelayLamaHandle *handle, float semitones) {
    if (!handle)
        return;
    EnterCriticalSection(&handle->lock);
    monk_synth_set_pitch_bend(handle->synth, clamp_range(semitones, -12.0f, 12.0f));
    LeaveCriticalSection(&handle->lock);
}

void dl_set_envelope(DelayLamaHandle *handle, float attack_seconds, float decay_seconds,
                     float sustain, float release_seconds) {
    if (!handle)
        return;
    EnterCriticalSection(&handle->lock);
    monk_synth_set_attack(handle->synth, clamp_range(attack_seconds, 0.0f, 10.0f));
    monk_synth_set_decay(handle->synth, clamp_range(decay_seconds, 0.0f, 10.0f));
    monk_synth_set_sustain(handle->synth, clamp01(sustain));
    monk_synth_set_release(handle->synth, clamp_range(release_seconds, 0.0f, 10.0f));
    LeaveCriticalSection(&handle->lock);
}

void dl_set_unison(DelayLamaHandle *handle, int32_t voices, float detune_cents, float spread) {
    if (!handle)
        return;
    EnterCriticalSection(&handle->lock);
    monk_synth_set_unison(handle->synth, voices);
    monk_synth_set_unison_detune(handle->synth, clamp_range(detune_cents, 0.0f, 100.0f));
    monk_synth_set_unison_voice_spread(handle->synth, clamp01(spread));
    LeaveCriticalSection(&handle->lock);
}

void dl_set_xy_range(DelayLamaHandle *handle, float low_note, float high_note, int32_t snap) {
    if (!handle)
        return;
    low_note = clamp_range(low_note, 0.0f, 126.0f);
    high_note = clamp_range(high_note, low_note + 1.0f, 127.0f);
    EnterCriticalSection(&handle->lock);
    handle->xy_low_note = low_note;
    handle->xy_high_note = high_note;
    handle->xy_snap = snap ? 1u : 0u;
    LeaveCriticalSection(&handle->lock);
}

float dl_get_vowel(DelayLamaHandle *handle) {
    if (!handle)
        return 0.5f;
    EnterCriticalSection(&handle->lock);
    float result = monk_synth_get_vowel(handle->synth);
    LeaveCriticalSection(&handle->lock);
    return result;
}

float dl_get_pitch(DelayLamaHandle *handle) {
    if (!handle)
        return 0.5f;
    EnterCriticalSection(&handle->lock);
    float result = monk_synth_get_pitch_normalized(handle->synth);
    LeaveCriticalSection(&handle->lock);
    return result;
}

int32_t dl_is_active(DelayLamaHandle *handle) {
    if (!handle)
        return 0;
    EnterCriticalSection(&handle->lock);
    int32_t result = monk_synth_is_active(handle->synth);
    LeaveCriticalSection(&handle->lock);
    return result;
}

int32_t dl_midi_source_count(DelayLamaHandle *handle) {
    return handle ? handle->midi_sources : 0;
}
