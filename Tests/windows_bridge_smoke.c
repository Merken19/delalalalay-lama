#include "../Windows/standalone_bridge_win32.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    DelayLamaHandle *handle = dl_create(48000.0f);
    float left[512];
    float right[512];
    double energy = 0.0;

    if (!handle) {
        fprintf(stderr, "bridge: dl_create failed\n");
        return 1;
    }

    /* Avoid relying on physical MIDI devices or the long delay tap. */
    dl_set_delay(handle, 0.0f);
    dl_set_volume(handle, 1.0f);
    dl_set_glide(handle, 0.0f);
    dl_note_on(handle, 60, 1.0f);

    if (!dl_is_active(handle)) {
        fprintf(stderr, "bridge: note-on did not activate the synth\n");
        dl_destroy(handle);
        return 1;
    }

    for (int block = 0; block < 4; ++block) {
        memset(left, 0, sizeof(left));
        memset(right, 0, sizeof(right));
        dl_process(handle, left, right, 512);
        for (uint32_t i = 0; i < 512; ++i) {
            if (!isfinite(left[i]) || !isfinite(right[i])) {
                fprintf(stderr, "bridge: non-finite output at block %d sample %u\n", block, i);
                dl_destroy(handle);
                return 1;
            }
            energy += (double)left[i] * left[i] + (double)right[i] * right[i];
        }
    }

    if (energy <= 0.0) {
        fprintf(stderr, "bridge: note-on produced no measurable audio\n");
        dl_destroy(handle);
        return 1;
    }

    /* Exercise the UI-facing XY path without requiring a window or MIDI
     * device, then verify that all-notes-off reaches the synth. */
    dl_xy_begin(handle, 0.5f, 0.5f);
    if (!isfinite(dl_get_pitch(handle)) || !isfinite(dl_get_vowel(handle))) {
        fprintf(stderr, "bridge: XY state readback was non-finite\n");
        dl_destroy(handle);
        return 1;
    }
    dl_xy_end(handle);
    dl_all_notes_off(handle);
    if (dl_is_active(handle)) {
        fprintf(stderr, "bridge: all-notes-off left the default-envelope voice active\n");
        dl_destroy(handle);
        return 1;
    }

    /* A flag press on an idle voice starts at the flag's pitch. The bridge
     * used to sound its placeholder note 127 (12.5 kHz) and glide down from
     * there, a laser-like sweep on every press. 0.25 of the default C3..C5
     * flag is F#3, which dl_get_pitch (C3..C4) reports as 0.5. */
    dl_set_glide(handle, 0.5f);
    dl_xy_begin(handle, 0.25f, 0.5f);
    float highest = dl_get_pitch(handle);
    for (int block = 0; block < 10; ++block) {
        dl_process(handle, left, right, 512);
        highest = fmaxf(highest, dl_get_pitch(handle));
    }
    if (highest > 0.51f) {
        fprintf(stderr, "bridge: flag press swept down from above the flag (pitch %.3f)\n",
                highest);
        dl_destroy(handle);
        return 1;
    }

    /* A MIDI key played over the held flag sounds, and releasing it hands
     * the pitch back to the flag rather than to the placeholder note. */
    dl_set_glide(handle, 0.0f);
    dl_note_on(handle, 60, 1.0f);
    dl_process(handle, left, right, 512);
    if (dl_get_pitch(handle) < 0.99f) {
        fprintf(stderr, "bridge: a MIDI key over the flag did not take over the pitch\n");
        dl_destroy(handle);
        return 1;
    }
    dl_note_off(handle, 60);
    for (int block = 0; block < 30; ++block)
        dl_process(handle, left, right, 512);
    if (fabsf(dl_get_pitch(handle) - 0.5f) > 0.01f) {
        fprintf(stderr, "bridge: releasing a MIDI key did not return to the flag (pitch %.3f)\n",
                dl_get_pitch(handle));
        dl_destroy(handle);
        return 1;
    }
    dl_xy_end(handle);
    dl_all_notes_off(handle);

    /* Settings-window parameters, including out-of-range values, must keep
     * the output finite while the flag is played across a custom range. */
    dl_set_vibrato_rate(handle, 2.0f);
    dl_set_breath(handle, -1.0f);
    dl_set_delay_time(handle, 0.25f);
    dl_set_delay_feedback(handle, 1.5f);
    dl_set_envelope(handle, 0.01f, 0.05f, 0.6f, 0.02f);
    dl_set_unison(handle, 12, 25.0f, 0.4f);
    dl_set_xy_range(handle, 72.0f, 60.0f, 1);
    dl_xy_begin(handle, 0.0f, 1.0f);
    for (int block = 0; block < 8; ++block) {
        dl_xy_move(handle, (float)block / 7.0f, 0.5f);
        dl_process(handle, left, right, 512);
        for (uint32_t i = 0; i < 512; ++i) {
            if (!isfinite(left[i]) || !isfinite(right[i])) {
                fprintf(stderr, "bridge: non-finite output with settings at block %d\n", block);
                dl_destroy(handle);
                return 1;
            }
        }
    }
    dl_xy_end(handle);
    if (!dl_is_active(handle)) {
        fprintf(stderr, "bridge: a release envelope should keep the voice sounding briefly\n");
        dl_destroy(handle);
        return 1;
    }
    dl_set_envelope(handle, 0.0f, 0.0f, 1.0f, 0.0f);
    dl_set_unison(handle, 1, 0.0f, 0.0f);
    dl_all_notes_off(handle);

    printf("windows_bridge_smoke: PASS energy=%.6f midi_sources=%d\n", energy,
           (int)dl_midi_source_count(handle));
    dl_destroy(handle);
    return 0;
}
