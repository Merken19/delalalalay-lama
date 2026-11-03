#ifndef DELAY_LAMA_STANDALONE_BRIDGE_WIN32_H
#define DELAY_LAMA_STANDALONE_BRIDGE_WIN32_H

#include "../Sources/standalone_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Windows-only extensions used by the settings window. The shared header above
 * stays identical to the macOS bridge. */
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

#ifdef __cplusplus
}
#endif

#endif
