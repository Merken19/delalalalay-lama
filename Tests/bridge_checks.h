/* Bridge checks shared by every platform's bridge test. They use only the
 * public API in Sources/standalone_bridge.h, so the same checks run against
 * the Windows bridge (bridge_smoke.c), the macOS bridge on macOS
 * (bridge_smoke.c via Tests/run_macos_tests.sh), and the macOS bridge
 * compiled on Windows against stub Apple headers (macos_bridge_logic.c). */

#ifndef DELAY_LAMA_BRIDGE_CHECKS_H
#define DELAY_LAMA_BRIDGE_CHECKS_H

#include "../Sources/standalone_bridge.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Returns 0 on success. Writes the total output energy of the first note to
 * *energy_out (if not NULL). The handle stays usable and idle afterwards. */
static int run_bridge_checks(DelayLamaHandle *handle, double *energy_out) {
    float left[512];
    float right[512];
    double energy = 0.0;

    /* Avoid relying on physical MIDI devices or the long delay tap. */
    dl_set_delay(handle, 0.0f);
    dl_set_volume(handle, 1.0f);
    dl_set_glide(handle, 0.0f);
    dl_note_on(handle, 60, 1.0f);

    if (!dl_is_active(handle)) {
        fprintf(stderr, "bridge: note-on did not activate the synth\n");
        return 1;
    }

    for (int block = 0; block < 4; ++block) {
        memset(left, 0, sizeof(left));
        memset(right, 0, sizeof(right));
        dl_process(handle, left, right, 512);
        for (uint32_t i = 0; i < 512; ++i) {
            if (!isfinite(left[i]) || !isfinite(right[i])) {
                fprintf(stderr, "bridge: non-finite output at block %d sample %u\n", block, i);
                return 1;
            }
            energy += (double)left[i] * left[i] + (double)right[i] * right[i];
        }
    }

    if (energy <= 0.0) {
        fprintf(stderr, "bridge: note-on produced no measurable audio\n");
        return 1;
    }
    if (energy_out)
        *energy_out = energy;

    /* Exercise the UI-facing XY path without requiring a window or MIDI
     * device, then verify that all-notes-off reaches the synth. */
    dl_xy_begin(handle, 0.5f, 0.5f);
    if (!isfinite(dl_get_pitch(handle)) || !isfinite(dl_get_vowel(handle))) {
        fprintf(stderr, "bridge: XY state readback was non-finite\n");
        return 1;
    }
    dl_xy_end(handle);
    dl_all_notes_off(handle);
    if (dl_is_active(handle)) {
        fprintf(stderr, "bridge: all-notes-off left the default-envelope voice active\n");
        return 1;
    }

    /* Out-of-range note numbers are folded into 0..127 instead of writing
     * past the held-note table. */
    dl_note_on(handle, 200, 1.0f);
    dl_note_off(handle, 200);
    dl_all_notes_off(handle);
    if (dl_is_active(handle)) {
        fprintf(stderr, "bridge: an out-of-range note stayed active\n");
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
        return 1;
    }

    /* A MIDI key played over the held flag sounds, and releasing it hands
     * the pitch back to the flag rather than to the placeholder note. */
    dl_set_glide(handle, 0.0f);
    dl_note_on(handle, 60, 1.0f);
    dl_process(handle, left, right, 512);
    if (dl_get_pitch(handle) < 0.99f) {
        fprintf(stderr, "bridge: a MIDI key over the flag did not take over the pitch\n");
        return 1;
    }
    dl_note_off(handle, 60);
    for (int block = 0; block < 30; ++block)
        dl_process(handle, left, right, 512);
    if (fabsf(dl_get_pitch(handle) - 0.5f) > 0.01f) {
        fprintf(stderr, "bridge: releasing a MIDI key did not return to the flag (pitch %.3f)\n",
                dl_get_pitch(handle));
        return 1;
    }
    dl_xy_end(handle);
    dl_all_notes_off(handle);

    /* A flag range of one octave from C3 with semitone snap: 0.49 of the way
     * is F#3 (6.0 semitones after snapping), not 5.88. */
    dl_set_xy_range(handle, 48.0f, 60.0f, 1);
    dl_xy_begin(handle, 0.49f, 0.5f);
    for (int block = 0; block < 20; ++block)
        dl_process(handle, left, right, 512);
    if (fabsf(dl_get_pitch(handle) - 0.5f) > 0.002f) {
        fprintf(stderr, "bridge: semitone snap missed F#3 (pitch %.4f)\n", dl_get_pitch(handle));
        return 1;
    }
    dl_xy_end(handle);
    dl_all_notes_off(handle);
    dl_set_xy_range(handle, 48.0f, 72.0f, 0);

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
                return 1;
            }
        }
    }
    dl_xy_end(handle);
    if (!dl_is_active(handle)) {
        fprintf(stderr, "bridge: a release envelope should keep the voice sounding briefly\n");
        return 1;
    }
    dl_set_envelope(handle, 0.0f, 0.0f, 1.0f, 0.0f);
    dl_set_unison(handle, 1, 0.0f, 0.0f);
    dl_set_xy_range(handle, 48.0f, 72.0f, 0);
    dl_all_notes_off(handle);
    return 0;
}

#endif
