#ifndef DELAY_LAMA_STANDALONE_BRIDGE_H
#define DELAY_LAMA_STANDALONE_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Thread-safe front end to the shared DSP, implemented once per platform
 * (Sources/standalone_bridge.c for macOS/CoreMIDI,
 * Windows/standalone_bridge_win32.c for Windows/WinMM). Both implement every
 * function below with the same behavior. */

typedef struct DelayLamaHandle DelayLamaHandle;

DelayLamaHandle *dl_create(float sample_rate);
void dl_destroy(DelayLamaHandle *handle);
void dl_process(DelayLamaHandle *handle, float *left, float *right, uint32_t frames);

void dl_note_on(DelayLamaHandle *handle, uint8_t note, float velocity);
void dl_note_off(DelayLamaHandle *handle, uint8_t note);
void dl_all_notes_off(DelayLamaHandle *handle);
void dl_xy_begin(DelayLamaHandle *handle, float pitch, float vowel);
void dl_xy_move(DelayLamaHandle *handle, float pitch, float vowel);
void dl_xy_end(DelayLamaHandle *handle);

void dl_set_glide(DelayLamaHandle *handle, float value);
void dl_set_voice(DelayLamaHandle *handle, float value);
void dl_set_delay(DelayLamaHandle *handle, float value);
void dl_set_vibrato(DelayLamaHandle *handle, float value);
void dl_set_volume(DelayLamaHandle *handle, float value);

/* Settings-window parameters (0..1 unless noted). */
void dl_set_vibrato_rate(DelayLamaHandle *handle, float value);
void dl_set_breath(DelayLamaHandle *handle, float value);
void dl_set_delay_time(DelayLamaHandle *handle, float value);
void dl_set_delay_feedback(DelayLamaHandle *handle, float value);
void dl_set_envelope(DelayLamaHandle *handle, float attack_seconds, float decay_seconds,
                     float sustain, float release_seconds);
void dl_set_unison(DelayLamaHandle *handle, int32_t voices, float detune_cents, float spread);

/* Maps the flag's horizontal axis onto [low_note, high_note] (MIDI note
 * numbers). With snap set, the flag plays only whole semitones. */
void dl_set_xy_range(DelayLamaHandle *handle, float low_note, float high_note, int32_t snap);

float dl_get_vowel(DelayLamaHandle *handle);
float dl_get_pitch(DelayLamaHandle *handle);
int32_t dl_is_active(DelayLamaHandle *handle);
int32_t dl_midi_source_count(DelayLamaHandle *handle);

#ifdef __cplusplus
}
#endif

#endif
