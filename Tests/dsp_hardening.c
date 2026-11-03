/*
 * Robustness tests for the DSP hardening ported from monksynth
 * (reference/monksynth/cpp/tests/test_*.c) plus flag-style glide checks.
 * Uses synth_internal.h to look at the gain smoothers and scratch buffers.
 *
 * The checks do not use assert(): CTest runs the Release build, where
 * NDEBUG would compile them away.
 */

#include "delay.h"
#include "synth.h"
#include "synth_internal.h"
#include "voice.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define SR 44100.0f
#define FLAG_SR 48000.0f  /* audio_engine.h output rate */
#define FLAG_BLOCK 512u   /* audio_engine.h kAudioFrames */

static int g_failures;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond);              \
            g_failures++;                                                                          \
        }                                                                                          \
    } while (0)

static void *xcalloc(size_t count, size_t size) {
    void *p = calloc(count, size);
    if (!p) {
        fprintf(stderr, "allocation failed\n");
        exit(2);
    }
    return p;
}

static MonkSynthEngine *new_synth(float sample_rate) {
    MonkSynthEngine *s = monk_synth_new(sample_rate);
    if (!s) {
        fprintf(stderr, "monk_synth_new failed\n");
        exit(2);
    }
    return s;
}

static int all_finite(const float *b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++)
        if (!isfinite(b[i]))
            return 0;
    return 1;
}

static float peak_abs(const float *b, uint32_t n) {
    float peak = 0.0f;
    for (uint32_t i = 0; i < n; i++)
        if (fabsf(b[i]) > peak)
            peak = fabsf(b[i]);
    return peak;
}

/* ---- Block handling (test_synth.c) ---- */

/* Every sample of a call is written, starting at the pointer passed in. */
static void test_process_starts_at_split_point(void) {
    MonkSynthEngine *s = new_synth(SR);
    monk_synth_set_attack(s, 0.0f);

    float l[256], r[256];
    for (int i = 0; i < 256; i++)
        l[i] = r[i] = 123.0f; /* sentinel: every sample must be overwritten */

    monk_synth_process(s, l, r, 100);
    int silent = 1;
    for (int i = 0; i < 100; i++)
        silent &= l[i] == 0.0f && r[i] == 0.0f;
    CHECK(silent);

    monk_synth_note_on(s, 60, 1.0f);
    monk_synth_process(s, l + 100, r + 100, 156);

    int written = 1, nonzero = 0;
    for (int i = 100; i < 256; i++) {
        written &= l[i] != 123.0f && r[i] != 123.0f;
        if (l[i] != 0.0f)
            nonzero++;
    }
    CHECK(written);
    CHECK(nonzero > 0);

    monk_synth_free(s);
}

/* Splitting a block into many small calls must produce the same audio as
 * one call. No glide: the pitch-compensated output gain is re-targeted per
 * call, so a moving pitch legitimately differs (see the glide tests). */
static void test_process_split_equals_whole(void) {
    MonkSynthEngine *a = new_synth(SR);
    MonkSynthEngine *b = new_synth(SR);
    monk_synth_set_unison(a, 3);
    monk_synth_set_unison(b, 3);
    monk_synth_set_glide(a, 0.0f);
    monk_synth_set_glide(b, 0.0f);
    monk_synth_note_on(a, 57, 0.8f);
    monk_synth_note_on(b, 57, 0.8f);

    enum { N = 4096 };
    static float la[N], ra[N], lb[N], rb[N];
    monk_synth_process(a, la, ra, N);

    /* Irregular chunk sizes, including single samples. */
    const uint32_t sizes[] = {1, 7, 64, 1, 500, 3, 1000, 2};
    uint32_t pos = 0, k = 0;
    while (pos < N) {
        uint32_t n = sizes[k++ % 8];
        if (pos + n > N)
            n = N - pos;
        monk_synth_process(b, lb + pos, rb + pos, n);
        pos += n;
    }

    int same = 1;
    for (int i = 0; i < N; i++)
        same &= la[i] == lb[i] && ra[i] == rb[i];
    CHECK(same);

    monk_synth_free(a);
    monk_synth_free(b);
}

/* Blocks longer than the internal scratch buffer are rendered in full
 * (before the port, everything past MONK_MAX_BUF was left untouched). */
static void test_process_block_larger_than_scratch(void) {
    MonkSynthEngine *s = new_synth(SR);
    monk_synth_note_on(s, 60, 1.0f);

    enum { N = MONK_MAX_BUF + 1000 };
    static float l[N], r[N];
    for (int i = 0; i < N; i++)
        l[i] = r[i] = 123.0f;

    monk_synth_process(s, l, r, N);
    int written = 1;
    for (int i = 0; i < N; i++)
        written &= l[i] != 123.0f && r[i] != 123.0f;
    CHECK(written);
    CHECK(all_finite(l, N) && all_finite(r, N));

    monk_synth_free(s);
}

/* A unison change ramps the gain over a fixed time, not over one call, so
 * a tiny render right after the change must not jump to the new gain. */
static void test_unison_gain_ramp_is_time_based(void) {
    MonkSynthEngine *s = new_synth(SR);
    monk_synth_note_on(s, 60, 1.0f);

    float l[64], r[64];
    monk_synth_process(s, l, r, 64);
    CHECK(fabsf(s->current_voice_gain - 1.0f) <= 1e-6f);

    monk_synth_set_unison(s, 9); /* target 1/3 */
    monk_synth_process(s, l, r, 1);
    /* After one sample of a ~5 ms ramp we should have moved only a little. */
    CHECK(s->current_voice_gain > 0.95f);
    CHECK(s->current_voice_gain < 1.0f);

    /* ...and after a good while, we should have arrived. */
    for (int i = 0; i < 100; i++)
        monk_synth_process(s, l, r, 64);
    CHECK(fabsf(s->current_voice_gain - s->target_voice_gain) <= 1e-3f);

    monk_synth_free(s);
}

/* ---- Sample rate (test_synth.c) ---- */

/* 384 kHz used to size the grain at 7680 samples and write past the
 * 3840-entry window, decay and aspiration tables into the next voice. */
static void test_high_sample_rate_clamps_grain(void) {
    MonkSynthEngine *s = new_synth(384000.0f);
    int clamped = 1;
    for (int i = 0; i < MONK_MAX_UNISON; i++)
        clamped &= s->voices[i].grain_len == MONK_MAX_GRAIN;
    CHECK(clamped);
    monk_synth_set_unison(s, 10);
    monk_synth_note_on(s, 60, 1.0f);
    static float l[MONK_MAX_BUF], r[MONK_MAX_BUF];
    for (int b = 0; b < 8; b++)
        monk_synth_process(s, l, r, MONK_MAX_BUF);
    CHECK(all_finite(l, MONK_MAX_BUF) && all_finite(r, MONK_MAX_BUF));
    monk_synth_set_sample_rate(s, 192000.0f);
    CHECK(s->voices[0].grain_len == MONK_MAX_GRAIN);
    monk_synth_free(s);
}

/* A sample rate of 0 made the vibrato phase advance by inf and the wrap
 * loop spin forever; NaN and out-of-range rates fall back to 44.1 kHz. */
static void test_bad_sample_rate_falls_back(void) {
    const float bad[] = {0.0f, -48000.0f, NAN, INFINITY, -INFINITY, 1e9f, 999.0f};
    static float l[1024], r[1024];

    for (size_t k = 0; k < sizeof(bad) / sizeof(bad[0]); k++) {
        MonkSynthEngine *s = new_synth(bad[k]);
        CHECK(s->voices[0].sample_rate == 44100.0f);
        CHECK(s->delay.sample_rate == 44100.0f);
        CHECK(s->gain_coeff > 0.0f && s->gain_coeff <= 1.0f);
        monk_synth_note_on(s, 60, 1.0f);
        monk_synth_process(s, l, r, 1024);
        CHECK(all_finite(l, 1024) && all_finite(r, 1024));
        CHECK(peak_abs(l, 1024) > 0.0f);

        /* The same values through set_sample_rate on a running engine. */
        monk_synth_set_sample_rate(s, FLAG_SR);
        monk_synth_set_sample_rate(s, bad[k]);
        CHECK(s->voices[0].sample_rate == 44100.0f);
        CHECK(s->delay.sample_rate == 44100.0f);
        CHECK(isfinite(s->gain_coeff));
        monk_synth_process(s, l, r, 1024);
        CHECK(all_finite(l, 1024) && all_finite(r, 1024));
        monk_synth_free(s);
    }
}

/* ---- Parameters (test_synth.c, test_delay.c) ---- */

/* NaN and out-of-range parameter values must never reach a table index:
 * a NaN vowel or delay rate cast to INT_MIN on x86 and read 8 GiB below
 * the table. They are clamped at the setter instead. */
static void test_non_finite_parameters_are_clamped(void) {
    MonkSynthEngine *s = new_synth(SR);
    monk_synth_set_vowel(s, NAN);
    monk_synth_set_voice(s, NAN);
    monk_synth_set_delay_rate(s, NAN);
    monk_synth_set_delay_mix(s, INFINITY);
    monk_synth_set_delay_feedback(s, NAN);
    monk_synth_set_vibrato(s, NAN);
    monk_synth_set_vibrato_rate(s, -INFINITY);
    monk_synth_set_aspiration(s, NAN);
    monk_synth_set_pitch_bend(s, NAN);
    monk_synth_set_glide(s, 1e9f);
    monk_synth_set_volume(s, NAN);
    monk_synth_set_level(s, INFINITY);
    monk_synth_set_pitch_hz(s, 0.0f);
    monk_synth_set_pitch_hz(s, -5.0f);
    monk_synth_set_pitch_hz(s, NAN);
    CHECK(s->voices[0].glide_param <= 1.0f);
    CHECK(isfinite(s->voices[0].target_pitch));
    CHECK(s->cc_volume == 0.0f && s->level == 1.0f);
    CHECK(s->delay.feedback == 0.0f);
    monk_synth_set_glide(s, NAN);
    CHECK(s->voices[0].glide_param == 0.0f);

    /* Volume NaN mutes the output; bring it back to hear the rest. */
    monk_synth_set_volume(s, 0.5f);
    monk_synth_note_on(s, 60, 1.0f);
    static float l[MONK_MAX_BUF], r[MONK_MAX_BUF];
    for (int b = 0; b < 4; b++)
        monk_synth_process(s, l, r, MONK_MAX_BUF);
    CHECK(all_finite(l, MONK_MAX_BUF) && all_finite(r, MONK_MAX_BUF));
    CHECK(peak_abs(l, MONK_MAX_BUF) > 0.0f);
    monk_synth_free(s);
}

/* Pitch inputs are clamped to 8..20000 Hz at every setter, and the grain
 * period never drops below one sample, even with bend and vibrato on top
 * at the lowest accepted sample rate. */
static void test_pitch_inputs_are_clamped(void) {
    const float bad[] = {0.0f, -5.0f, NAN, INFINITY, -INFINITY, 1e30f, 1e-30f};
    const float lo = monk_hz_to_note(8.0f), hi = monk_hz_to_note(20000.0f);
    static float l[2048], r[2048];

    MonkVoice *v = xcalloc(1, sizeof(*v));
    for (size_t k = 0; k < sizeof(bad) / sizeof(bad[0]); k++) {
        monk_voice_init(v, SR);
        monk_voice_note_on(v, bad[k], 1.0f);
        CHECK(v->target_pitch >= lo && v->target_pitch <= hi);
        CHECK(v->current_pitch >= lo && v->current_pitch <= hi);
        monk_voice_set_pitch_direct(v, bad[k]);
        CHECK(v->target_pitch >= lo && v->target_pitch <= hi);
        CHECK(v->current_pitch >= lo && v->current_pitch <= hi);
        monk_voice_set_pitch_target(v, bad[k]);
        CHECK(v->target_pitch >= lo && v->target_pitch <= hi);
        CHECK(isfinite(v->pitch_step));
    }
    free(v);

    MonkSynthEngine *s = new_synth(MONK_MIN_SAMPLE_RATE);
    monk_synth_set_vibrato(s, 1.0f);
    monk_synth_set_pitch_bend(s, 12.0f);
    monk_synth_note_on(s, 127, 1.0f);
    for (size_t k = 0; k < sizeof(bad) / sizeof(bad[0]); k++) {
        monk_synth_set_pitch_hz(s, bad[k]);
        monk_synth_process(s, l, r, 2048);
        CHECK(all_finite(l, 2048) && all_finite(r, 2048));
    }
    monk_synth_set_pitch_hz(s, 20000.0f);
    monk_synth_process(s, l, r, 2048);
    CHECK(all_finite(l, 2048) && all_finite(r, 2048));
    monk_synth_free(s);
}

/* The vibrato wrap is one fmodf step, so a non-finite phase recovers
 * instead of looping forever. */
static void test_vibrato_phase_recovers_from_non_finite(void) {
    const float bad[] = {INFINITY, NAN, 1e30f};
    static float l[1024], r[1024];

    for (size_t k = 0; k < sizeof(bad) / sizeof(bad[0]); k++) {
        MonkSynthEngine *s = new_synth(SR);
        monk_synth_set_vibrato(s, NAN);
        monk_synth_set_vibrato_rate(s, INFINITY);
        CHECK(s->voices[0].vibrato_depth == 0.0f);
        CHECK(s->voices[0].vibrato_rate == 1.0f);
        monk_synth_note_on(s, 60, 1.0f);
        s->voices[0].vibrato_phase = bad[k];
        monk_synth_process(s, l, r, 1024);
        CHECK(s->voices[0].vibrato_phase >= 0.0f &&
              s->voices[0].vibrato_phase < (float)MONK_SINE_TBL_SIZE);
        CHECK(all_finite(l, 1024) && all_finite(r, 1024));
        monk_synth_free(s);
    }
}

/* reset() re-arms the gain smoothers so nothing non-finite can linger. */
static void test_reset_rearms_gain_smoothing(void) {
    MonkSynthEngine *s = new_synth(SR);
    s->current_out_gain = NAN;
    s->current_voice_gain = NAN;
    monk_synth_reset(s);
    CHECK(s->current_out_gain < 0.0f);
    CHECK(isfinite(s->current_voice_gain));
    monk_synth_note_on(s, 60, 1.0f);
    static float l[1024], r[1024];
    monk_synth_process(s, l, r, 1024);
    CHECK(all_finite(l, 1024) && all_finite(r, 1024));
    CHECK(peak_abs(l, 1024) > 0.0f);
    monk_synth_free(s);
}

/* ---- Delay (test_delay.c) ---- */

static void test_delay_setters_clamp(void) {
    MonkDelay *d = xcalloc(1, sizeof(*d));
    monk_delay_init(d, SR);

    monk_delay_set_rate(d, 2.0f);
    CHECK(d->rate == 1.0f);
    monk_delay_set_rate(d, -1.0f);
    CHECK(d->rate == 0.0f);
    monk_delay_set_mix(d, 2.0f);
    CHECK(d->mix == 1.0f);
    monk_delay_set_mix(d, -1.0f);
    CHECK(d->mix == 0.0f);
    monk_delay_set_feedback(d, 2.0f);
    CHECK(d->feedback == 0.95f);
    monk_delay_set_feedback(d, -1.0f);
    CHECK(d->feedback == 0.0f);
    monk_delay_set_feedback(d, INFINITY);
    CHECK(d->feedback == 0.95f);
    monk_delay_set_feedback(d, NAN);
    CHECK(d->feedback == 0.0f);

    free(d);
}

/* At the feedback cap an impulse decays instead of blowing up. */
static void test_delay_feedback_stability(void) {
    MonkDelay *d = xcalloc(1, sizeof(*d));
    monk_delay_init(d, SR);
    monk_delay_set_mix(d, 1.0f);
    monk_delay_set_feedback(d, 1.0f); /* capped at 0.95 */

    enum { N = 88200 }; /* 2 seconds */
    float *in = xcalloc(N, sizeof(float));
    float *l = xcalloc(N, sizeof(float));
    float *r = xcalloc(N, sizeof(float));
    in[0] = 1.0f;

    monk_delay_process(d, in, l, r, N);
    CHECK(all_finite(l, N) && all_finite(r, N));
    CHECK(peak_abs(l, N) < 10.0f && peak_abs(r, N) < 10.0f);

    free(in);
    free(l);
    free(r);
    free(d);
}

/* At some rate/sample-rate pairs the read position is a tiny negative that
 * rounds to exactly 96000.0 once the line size is added, one past the end
 * of the buffer. The index is now split from the fraction before wrapping. */
static void test_delay_read_index_never_wraps_past_end(void) {
    MonkDelay *d = xcalloc(1, sizeof(*d));
    monk_delay_init(d, SR);
    monk_delay_set_rate(d, 0.0000270f);
    monk_delay_set_sample_rate(d, SR);
    static float in[1024], l[1024], r[1024];
    in[0] = 1.0f;
    int finite = 1;
    for (int b = 0; b < MONK_DELAY_LINE_SIZE / 1024 + 2; b++) {
        monk_delay_process(d, in, l, r, 1024);
        finite &= all_finite(l, 1024) && all_finite(r, 1024);
    }
    CHECK(finite);
    free(d);
}

/* NaN used to pass the rate and mix range checks and poison the read
 * position permanently; sample rate 0 made the smoothing coefficient inf. */
static void test_delay_non_finite_inputs_are_clamped(void) {
    MonkDelay *d = xcalloc(1, sizeof(*d));
    monk_delay_init(d, 0.0f);
    CHECK(d->sample_rate == 44100.0f);
    CHECK(isfinite(d->smooth_coeff));
    monk_delay_set_rate(d, NAN);
    monk_delay_set_mix(d, NAN);
    CHECK(d->rate == 0.0f && d->mix == 0.0f);
    CHECK(isfinite(d->target_delay_l) && isfinite(d->target_delay_r));
    monk_delay_set_sample_rate(d, NAN);
    CHECK(d->sample_rate == 44100.0f);
    CHECK(isfinite(d->smooth_coeff));
    static float in[256], l[256], r[256];
    in[0] = 1.0f;
    monk_delay_process(d, in, l, r, 256);
    CHECK(all_finite(l, 256) && all_finite(r, 256));
    free(d);
}

/* ---- Flag glides ---- */

/* A slide on the flag the way the bridge plays it (dl_xy_begin/dl_xy_move):
 * monk_synth_set_pitch_hz with the XY pad's minimum glide, at the app's
 * rate and default levels. Pitch events land on FLAG_BLOCK boundaries, so
 * every chunking sees them at the same sample. */
typedef struct {
    const char *name;
    float glide;          /* portamento knob, 0-1 */
    float from_hz, to_hz; /* C3..C5 is the default flag range */
    uint32_t move_blocks; /* 0: one jump; else one pitch update per block */
} Slide;

static const Slide kSlides[] = {
    {"slow slide up", 0.5f, 130.81f, 523.25f, 90},
    {"fast slide down", 0.5f, 523.25f, 130.81f, 0},
    {"max glide up", 1.0f, 130.81f, 523.25f, 0},
};

#define SLIDE_START_BLOCK 8u
#define SLIDE_BLOCKS 128u
#define SLIDE_SAMPLES (SLIDE_BLOCKS * FLAG_BLOCK)

/* Render the slide in calls of `chunk` samples (a divisor of FLAG_BLOCK).
 * If `gain` is given, it receives the per-sample output gain recovered
 * from the pre-gain scratch buffer (exact to float rounding), or NAN where
 * that sample is zero or denormal. */
static void render_slide(const Slide *sl, uint32_t chunk, float *l, float *r, float *gain) {
    MonkSynthEngine *s = new_synth(FLAG_SR);
    monk_synth_set_volume(s, 0.28f); /* bridge defaults */
    monk_synth_set_level(s, 0.75f);
    monk_synth_set_vibrato(s, 0.15f);
    monk_synth_set_delay_mix(s, 0.42f);
    monk_synth_set_glide(s, sl->glide);
    monk_synth_note_on(s, (uint8_t)lroundf(monk_hz_to_note(sl->from_hz)), 1.0f);
    monk_synth_set_pitch_hz(s, sl->from_hz);

    for (uint32_t pos = 0; pos < SLIDE_SAMPLES; pos += chunk) {
        if (pos % FLAG_BLOCK == 0) {
            uint32_t b = pos / FLAG_BLOCK;
            if (b >= SLIDE_START_BLOCK && b <= SLIDE_START_BLOCK + sl->move_blocks) {
                float t = sl->move_blocks ? (float)(b - SLIDE_START_BLOCK) / (float)sl->move_blocks
                                          : 1.0f;
                monk_synth_set_pitch_hz(s, sl->from_hz * powf(sl->to_hz / sl->from_hz, t));
            }
        }
        monk_synth_process(s, l + pos, r + pos, chunk);
        if (gain) {
            for (uint32_t i = 0; i < chunk; i++)
                gain[pos + i] = fabsf(s->scratch_l[i]) > 1e-30f ? l[pos + i] / s->scratch_l[i] : NAN;
        }
    }
    monk_synth_free(s);
}

/* The same glide rendered in 1-, 64- and 512-sample calls stays nearly
 * identical. Only the output gain target is per call (as in monksynth),
 * and the ~5 ms smoother keeps that difference small: the fast slide
 * (two octaves in 100 ms, ~4.6% gain change per 512-sample block) differs
 * by about a third of one block's change, ~1.5% of peak. */
static void test_glide_is_nearly_chunk_independent(void) {
    static float l512[SLIDE_SAMPLES], r512[SLIDE_SAMPLES];
    static float l[SLIDE_SAMPLES], r[SLIDE_SAMPLES];
    const uint32_t chunks[] = {1, 64};

    for (size_t k = 0; k < sizeof(kSlides) / sizeof(kSlides[0]); k++) {
        render_slide(&kSlides[k], FLAG_BLOCK, l512, r512, NULL);
        CHECK(all_finite(l512, SLIDE_SAMPLES) && all_finite(r512, SLIDE_SAMPLES));
        float peak = fmaxf(peak_abs(l512, SLIDE_SAMPLES), peak_abs(r512, SLIDE_SAMPLES));
        CHECK(peak > 0.01f);

        for (size_t c = 0; c < sizeof(chunks) / sizeof(chunks[0]); c++) {
            render_slide(&kSlides[k], chunks[c], l, r, NULL);
            float max_diff = 0.0f;
            double diff_energy = 0.0, energy = 0.0;
            for (uint32_t i = 0; i < SLIDE_SAMPLES; i++) {
                float dl = fabsf(l[i] - l512[i]), dr = fabsf(r[i] - r512[i]);
                max_diff = fmaxf(max_diff, fmaxf(dl, dr));
                diff_energy += (double)dl * dl + (double)dr * dr;
                energy += (double)l512[i] * l512[i] + (double)r512[i] * r512[i];
            }
            float rel_rms = (float)sqrt(diff_energy / energy);
            printf("  %-15s %3u vs 512: max diff %.4f%% of peak, rms diff %.4f%%\n",
                   kSlides[k].name, chunks[c], 100.0f * max_diff / peak, 100.0f * rel_rms);
            CHECK(max_diff <= 0.02f * peak); /* the held per-block gain was ~2.5% */
            CHECK(rel_rms <= 0.005f);
        }
    }
}

/* During a glide the pitch-compensation gain must not step at block
 * boundaries. Before the port it was held for a whole block and jumped
 * to the next value at the boundary (zipper noise while sliding). */
static void test_glide_gain_has_no_block_steps(void) {
    static float l[SLIDE_SAMPLES], r[SLIDE_SAMPLES], g[SLIDE_SAMPLES];

    for (size_t k = 0; k < sizeof(kSlides) / sizeof(kSlides[0]); k++) {
        render_slide(&kSlides[k], FLAG_BLOCK, l, r, g);

        float max_step = 0.0f;  /* largest relative change between two samples */
        float max_block = 0.0f; /* largest relative change over one block */
        uint32_t measured = 0;
        for (uint32_t b = SLIDE_START_BLOCK; b < SLIDE_BLOCKS; b++) {
            uint32_t pos = b * FLAG_BLOCK;
            for (uint32_t i = pos; i < pos + FLAG_BLOCK; i++) {
                if (isfinite(g[i - 1]) && isfinite(g[i]) && g[i - 1] > 0.0f)
                    max_step = fmaxf(max_step, fabsf(g[i] - g[i - 1]) / g[i - 1]);
            }
            float g0 = g[pos - 1], g1 = g[pos + FLAG_BLOCK - 1];
            if (isfinite(g0) && isfinite(g1) && g0 > 0.0f) {
                max_block = fmaxf(max_block, fabsf(g1 - g0) / g0);
                measured++;
            }
        }
        printf("  %-15s largest gain step %.5f%%, largest change per block %.3f%%\n",
               kSlides[k].name, 100.0f * max_step, 100.0f * max_block);
        CHECK(measured > SLIDE_BLOCKS / 2);
        /* The slide moves the gain enough that a held gain would step. */
        CHECK(max_block > 1e-3f);
        /* A held gain steps by the whole per-block change at the boundary;
         * the one-pole smoother spreads it to well under a twentieth. */
        CHECK(max_step <= 0.05f * max_block);
        CHECK(max_step <= 1e-3f);
    }
}

/* The flag holds placeholder note 127 while pressed. Its note-on must start
 * an idle voice at the flag's pitch: through monk_synth_note_on it started
 * at note 127 (12.5 kHz) and glided down to the flag, a laser-like sweep.
 * Every unison voice starts at its detuned flag pitch, and the placeholder
 * stays on the stack like any other key. */
static void test_flag_note_on_starts_at_flag_pitch(void) {
    MonkSynthEngine *s = new_synth(FLAG_SR);
    float l[FLAG_BLOCK], r[FLAG_BLOCK];
    const float flag_hz = monk_note_to_hz(60.0f);
    monk_synth_set_glide(s, 1.0f); /* longest glide: the old sweep took ~1 s */
    monk_synth_set_unison(s, 3);
    monk_synth_set_unison_detune(s, 20.0f); /* voices at -0.2, 0, +0.2 semitones */

    monk_synth_note_on_hz(s, 127, flag_hz, 1.0f); /* as dl_xy_begin does */
    monk_synth_set_pitch_hz(s, flag_hz);
    CHECK(monk_synth_top_note(s) == 127);
    float lowest = 1000.0f, highest = -1000.0f;
    for (int b = 0; b < 20; b++) {
        for (int v = 0; v < 3; v++) {
            lowest = fminf(lowest, s->voices[v].current_pitch);
            highest = fmaxf(highest, s->voices[v].current_pitch);
        }
        monk_synth_process(s, l, r, FLAG_BLOCK);
    }
    CHECK(lowest > 59.75f && highest < 60.25f);
    CHECK(peak_abs(l, FLAG_BLOCK) > 0.0f);

    /* A key pressed over the flag sounds; releasing it uncovers 127. */
    monk_synth_note_on(s, 64, 1.0f);
    CHECK(monk_synth_top_note(s) == 64);
    CHECK(fabsf(s->voices[1].target_pitch - 64.0f) < 1e-3f);
    monk_synth_note_off(s, 64);
    CHECK(monk_synth_top_note(s) == 127);
    monk_synth_note_off(s, 127);
    CHECK(monk_synth_top_note(s) == -1);
    CHECK(!monk_synth_is_active(s));
    monk_synth_free(s);
}

typedef struct {
    const char *name;
    void (*fn)(void);
} TestCase;

int main(void) {
    const TestCase tests[] = {
        {"process starts at split point", test_process_starts_at_split_point},
        {"process split equals whole", test_process_split_equals_whole},
        {"process block larger than scratch", test_process_block_larger_than_scratch},
        {"unison gain ramp is time based", test_unison_gain_ramp_is_time_based},
        {"high sample rate clamps grain", test_high_sample_rate_clamps_grain},
        {"bad sample rate falls back", test_bad_sample_rate_falls_back},
        {"non-finite parameters are clamped", test_non_finite_parameters_are_clamped},
        {"pitch inputs are clamped", test_pitch_inputs_are_clamped},
        {"vibrato phase recovers from non-finite", test_vibrato_phase_recovers_from_non_finite},
        {"reset re-arms gain smoothing", test_reset_rearms_gain_smoothing},
        {"delay setters clamp", test_delay_setters_clamp},
        {"delay feedback stability", test_delay_feedback_stability},
        {"delay read index never wraps past end", test_delay_read_index_never_wraps_past_end},
        {"delay non-finite inputs are clamped", test_delay_non_finite_inputs_are_clamped},
        {"glide is nearly chunk independent", test_glide_is_nearly_chunk_independent},
        {"glide gain has no block steps", test_glide_gain_has_no_block_steps},
        {"flag note-on starts at flag pitch", test_flag_note_on_starts_at_flag_pitch},
    };
    const int count = (int)(sizeof(tests) / sizeof(tests[0]));
    int passed = 0;

    for (int i = 0; i < count; i++) {
        int before = g_failures;
        tests[i].fn();
        if (g_failures == before) {
            printf("[PASS] %s\n", tests[i].name);
            passed++;
        } else {
            fprintf(stderr, "[FAIL] %s\n", tests[i].name);
        }
    }

    printf("dsp_hardening: %d/%d tests passed\n", passed, count);
    return passed == count ? 0 : 1;
}
