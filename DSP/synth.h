/*
 * MonkSynthEngine — public C API.
 *
 * MonkSynthEngine manages the vocal synthesizer, stereo delay, monophonic note
 * stack, and MIDI CC routing. All calls must be on the same thread.
 */

#ifndef MONK_SYNTH_H
#define MONK_SYNTH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MonkSynthEngine MonkSynthEngine;

/* Lifecycle */
MonkSynthEngine *monk_synth_new(float sample_rate);
void monk_synth_free(MonkSynthEngine *s);
void monk_synth_set_sample_rate(MonkSynthEngine *s, float sample_rate);
void monk_synth_reset(MonkSynthEngine *s);

/* Note control */
void monk_synth_note_on(MonkSynthEngine *s, uint8_t note, float velocity);
void monk_synth_note_on_hz(MonkSynthEngine *s, uint8_t note, float hz, float velocity);
void monk_synth_note_off(MonkSynthEngine *s, uint8_t note);
void monk_synth_set_pitch_hz(MonkSynthEngine *s, float hz);
void monk_synth_restore_note_stack(MonkSynthEngine *s);

/* Parameters (0.0–1.0) */
void monk_synth_set_vowel(MonkSynthEngine *s, float value);
void monk_synth_set_voice(MonkSynthEngine *s, float value);
void monk_synth_set_glide(MonkSynthEngine *s, float value);
void monk_synth_set_vibrato(MonkSynthEngine *s, float value);
void monk_synth_set_vibrato_rate(MonkSynthEngine *s, float value);
void monk_synth_set_pitch_bend(MonkSynthEngine *s, float semitones);
void monk_synth_set_aspiration(MonkSynthEngine *s, float value);
void monk_synth_set_attack(MonkSynthEngine *s, float seconds);
void monk_synth_set_decay(MonkSynthEngine *s, float seconds);
void monk_synth_set_sustain(MonkSynthEngine *s, float level);
void monk_synth_set_release(MonkSynthEngine *s, float seconds);
void monk_synth_set_unison(MonkSynthEngine *s, int count);
void monk_synth_set_unison_detune(MonkSynthEngine *s, float cents);
void monk_synth_set_unison_voice_spread(MonkSynthEngine *s, float spread);
void monk_synth_set_delay_mix(MonkSynthEngine *s, float value);
void monk_synth_set_delay_rate(MonkSynthEngine *s, float value);
void monk_synth_set_delay_feedback(MonkSynthEngine *s, float value);
void monk_synth_set_volume(MonkSynthEngine *s, float value);
void monk_synth_set_level(MonkSynthEngine *s, float value);
/* Addition (Delay Lama Everything D18): restarts the vibrato's random jitter
 * from `seed` (unison voice i gets seed + i * 7919, as at creation), so
 * several synths don't wobble in lockstep. Never called by default. */
void monk_synth_set_vibrato_seed(MonkSynthEngine *s, uint32_t seed);
/* Addition (Delay Lama Everything D18): formant bandwidths times `scale`
 * (0.25-8; 1 = the built-in 32.5 / 47.5 / 62.5 Hz, today's sound). Wider
 * formants ring shorter in each grain. Never called by default. */
void monk_synth_set_formant_bandwidth(MonkSynthEngine *s, float scale);
/* Addition (Delay Lama Everything D18, phase 4c): formant `formant` (0-2:
 * F1-F3) at `scale` (0-2) times its built-in frequencies in every voice; 1 =
 * today's sound exactly, 0 silences that formant. Never called by default. */
void monk_synth_set_formant_scale(MonkSynthEngine *s, int formant, float scale);
/* Addition (Delay Lama Everything, phase 4c): the vibrato LFO of every unison
 * voice (phase, jittered rate, jitter counter, random state). A synth given
 * another's state wobbles in lockstep with it from then on, as long as both
 * sing with the same vibrato depth and rate, so the intervals of a chord sung
 * by several synths stay put. Never called by default. */
#define MONK_VIBRATO_VOICES 10
typedef struct MonkVibratoState {
    float phase[MONK_VIBRATO_VOICES];
    float jitter[MONK_VIBRATO_VOICES];
    uint32_t counter[MONK_VIBRATO_VOICES];
    uint32_t rng[MONK_VIBRATO_VOICES];
} MonkVibratoState;
void monk_synth_get_vibrato_state(const MonkSynthEngine *s, MonkVibratoState *state);
void monk_synth_set_vibrato_state(MonkSynthEngine *s, const MonkVibratoState *state);

/* MIDI routing */
void monk_synth_midi_cc(MonkSynthEngine *s, uint8_t cc, float value);

/* State readback (for UI animation) */
float monk_synth_get_vowel(MonkSynthEngine *s);
float monk_synth_get_pitch_normalized(MonkSynthEngine *s);

/* Audio processing */
void monk_synth_process(MonkSynthEngine *s, float *out_l, float *out_r, uint32_t num_samples);

/* State query */
int32_t monk_synth_is_active(const MonkSynthEngine *s);
float monk_synth_amplitude(const MonkSynthEngine *s);
int32_t monk_synth_top_note(const MonkSynthEngine *s);

/* Utility */
float monk_midi_note_to_freq(uint8_t note);

#ifdef __cplusplus
}
#endif

#endif
