#ifndef DELAY_LAMA_VOICE_BRIDGE_H
#define DELAY_LAMA_VOICE_BRIDGE_H

#include "../Sources/standalone_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Windows-only additions to the bridge for DelayLamaVoice.exe, the helper that
 * sings a MIDI channel fed through its stdin. Implemented in
 * standalone_bridge_win32.c; the standalone does not use them, so the macOS
 * bridge has no counterpart. */

/* Like dl_create, but opens no WinMM MIDI input: the handle only hears the
 * calls made on it. dl_midi_source_count reports 0. */
DelayLamaHandle *dl_create_without_midi(float sample_rate);

/* Real pitch bend in semitones, clamped by the DSP to -12..12. (The
 * standalone's MIDI input maps the pitch wheel to the vowel instead.) */
void dl_set_pitch_bend(DelayLamaHandle *handle, float semitones);

/* Output level 0..1 on top of the volume; the DSP smooths it over ~5 ms. */
void dl_set_level(DelayLamaHandle *handle, float value);

/* Vowel 0..1, as the flag's vertical axis sets it. */
void dl_set_vowel(DelayLamaHandle *handle, float value);

/* Own vibrato jitter seed for this synth (monk_synth_set_vibrato_seed). */
void dl_set_vibrato_seed(DelayLamaHandle *handle, uint32_t seed);

/* Formant bandwidths times `scale` (monk_synth_set_formant_bandwidth;
 * 1 = the DSP's own, 0.25-8). */
void dl_set_formant_bandwidth(DelayLamaHandle *handle, float scale);

/* Sets the DSP note to `note` (0-126) with no note stack: the articulator's
 * monks sing one note each. A sounding voice glides there at the current
 * glide; a silent one starts on it. */
void dl_voice_note_on(DelayLamaHandle *handle, uint8_t note);

/* Releases every note this handle holds (dl_all_notes_off without the flag). */
void dl_voice_note_off(DelayLamaHandle *handle);

#ifdef __cplusplus
}
#endif

#endif
