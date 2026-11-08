// Checks for the DelayLamaVoice choir and its offline render
// (Windows/voice_choir.h, Windows/voice_render.h): determinism, timing,
// levels, real pitch bend. No assert(): CTest runs Release builds.

#include "voice_choir.h"
#include "voice_render.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

using namespace delay_lama::voice;

namespace {

int failures = 0;

void check(bool condition, const std::string &what) {
    if (!condition) {
        std::fprintf(stderr, "voice_render: FAIL %s\n", what.c_str());
        ++failures;
    }
}

bool render(const std::string &script, RenderResult &result, int monks = 4,
            double tail_ms = 0.0) {
    std::istringstream input(script);
    std::string error;
    const bool ok = render_script(input, monks, tail_ms, result, error);
    check(ok, "script renders (" + error + ")");
    return ok;
}

std::string render_error(const std::string &script) {
    std::istringstream input(script);
    RenderResult result;
    std::string error;
    check(!render_script(input, 4, 0.0, result, error), "bad script is rejected");
    return error;
}

size_t frame_at(double ms) { return static_cast<size_t>(ms * kSampleRate / 1000.0); }

// Sum of squares of both channels over [from_ms, to_ms).
double energy(const RenderResult &result, double from_ms, double to_ms) {
    double sum = 0.0;
    const size_t end = std::min(result.pcm.size() / 2, frame_at(to_ms));
    for (size_t frame = frame_at(from_ms); frame < end; ++frame) {
        for (int channel = 0; channel < 2; ++channel) {
            const double sample = result.pcm[frame * 2 + channel] / 32768.0;
            sum += sample * sample;
        }
    }
    return sum;
}

// Fundamental in Hz from the normalised autocorrelation of the left channel
// over [from_ms, from_ms + 100 ms), searching 80..400 Hz.
double pitch_hz(const RenderResult &result, double from_ms) {
    const size_t start = frame_at(from_ms);
    const size_t length = frame_at(100.0);
    const size_t min_lag = static_cast<size_t>(kSampleRate / 400.0);
    const size_t max_lag = static_cast<size_t>(kSampleRate / 80.0);
    if ((start + length + max_lag) * 2 > result.pcm.size())
        return 0.0;
    const auto sample = [&](size_t frame) { return result.pcm[frame * 2] / 32768.0; };
    double best = -1.0;
    size_t best_lag = 0;
    for (size_t lag = min_lag; lag <= max_lag; ++lag) {
        double cross = 0.0;
        double power_a = 0.0;
        double power_b = 0.0;
        for (size_t index = start; index < start + length; ++index) {
            cross += sample(index) * sample(index + lag);
            power_a += sample(index) * sample(index);
            power_b += sample(index + lag) * sample(index + lag);
        }
        const double score = cross / std::sqrt(power_a * power_b + 1e-30);
        if (score > best) {
            best = score;
            best_lag = lag;
        }
    }
    return best_lag ? kSampleRate / static_cast<double>(best_lag) : 0.0;
}

const char kBusyScript[] = "hello 0\n"
                           "cc 7 110\n"
                           "on 60 100\n"
                           "wait 120\n"
                           "on 64 90\n"
                           "on 67 80\n"
                           "wait 200\n"
                           "bendrange 2\n"
                           "bend 12000\n"
                           "wait 50.5\n"
                           "bend 8192\n"
                           "cc 64 127\n"
                           "off 64\n"
                           "on 72 127\n"
                           "on 76 60\n" // steals: the choir of 4 is full
                           "wait 300\n"
                           "cc 64 0\n"
                           "set vowel 0.2\n"
                           "set unison_voices 3\n"
                           "wait 200\n"
                           "allnotesoff\n"
                           "wait 100\n";

void determinism_and_timing() {
    RenderResult first;
    RenderResult second;
    if (!render(kBusyScript, first, 4, 250.0) || !render(kBusyScript, second, 4, 250.0))
        return;
    const double total_ms = 120 + 200 + 50.5 + 300 + 200 + 100 + 250;
    check(first.pcm.size() == 2 * static_cast<size_t>(std::llround(total_ms * 48.0)),
          "the render is as long as its waits plus the tail");
    check(first.pcm == second.pcm, "the same script renders the same samples");
    check(energy(first, 0, 120) > 0.0, "the first note sounds");
    check(first.clipped == 0, "the busy script does not clip");

    RenderResult three_monks;
    if (render(kBusyScript, three_monks, 3, 250.0))
        check(three_monks.pcm != first.pcm, "the choir size changes the result");
}

void silence_and_levels() {
    RenderResult result;
    if (!render("set echo_amount 0\nwait 100\non 60 100\nwait 200\nallnotesoff\nwait 200\n",
                result))
        return;
    check(energy(result, 0, 100) == 0.0, "nothing sounds before the first note");
    check(energy(result, 120, 300) > 1.0, "a note sounds");
    check(energy(result, 350, 500) < 1e-6, "allnotesoff silences every monk");

    RenderResult loud;
    RenderResult soft;
    RenderResult muted;
    const char kNote[] = "set echo_amount 0\non 60 %d\nwait 300\n";
    char script[128];
    std::snprintf(script, sizeof(script), kNote, 127);
    render(script, loud);
    std::snprintf(script, sizeof(script), kNote, 40);
    render(script, soft);
    render("set echo_amount 0\ncc 7 0\non 60 127\nwait 300\n", muted);
    const double loud_energy = energy(loud, 50, 300);
    check(loud_energy > 20.0 * energy(soft, 50, 300), "velocity scales the level");
    check(energy(muted, 0, 300) < 1e-9, "CC7 0 mutes the channel");

    RenderResult chord;
    render("set echo_amount 0\non 60 127\non 64 127\non 67 127\nwait 300\n", chord);
    check(energy(chord, 50, 300) > 1.5 * loud_energy, "a chord is sung by several monks");

    RenderResult released;
    render("set echo_amount 0\nset release 0.3\non 60 127\nwait 100\noff 60\nwait 150\n",
           released);
    check(energy(released, 150, 250) > 0.01 * energy(released, 0, 100),
          "a release envelope keeps a released monk sounding");
}

// After a note-off the DSP lets the last grains play out (up to 20 ms, longer
// at low pitches). That tail keeps the level it had at the note-off: a gain or
// CC11 raised before the next note used to replay it at full level, a blip of
// the old note just before the new one (the "laser" onsets heard in the phase
// 4a bass renders).
void release_tail_keeps_its_level() {
    const char *lower[] = {"set gain 0.01\n", "cc 11 13\n"};
    const char *raise[] = {"set gain 1\n", "cc 11 127\n"};
    for (int index = 0; index < 2; ++index) {
        RenderResult result;
        render(std::string("set echo_amount 0\non 45 100\nwait 300\n") + lower[index] +
                   "wait 50\noff 45\n" + raise[index] + "wait 15\non 45 100\nwait 100\n",
               result);
        const double tail = energy(result, 350, 365) / 15.0;
        const double note = energy(result, 385, 465) / 80.0;
        std::printf("voice_render: tail after %s at %.1f dB of the next note\n",
                    index == 0 ? "set gain" : "CC11", 10.0 * std::log10(tail / note + 1e-30));
        check(note > 0.0 && tail < 1e-3 * note,
              std::string("a release tail stays quiet when ") +
                  (index == 0 ? "the gain" : "CC11") + " rises before the next note");
    }

    // Lowering the level still reaches a tail (a fade over a release).
    RenderResult fading;
    render("set echo_amount 0\nset release 0.5\non 45 100\nwait 200\noff 45\nwait 100\n"
           "set gain 0\nwait 100\n",
           fading);
    check(energy(fading, 250, 300) > 0.0 && energy(fading, 360, 400) < 1e-9,
          "lowering the gain still quiets a release tail");
}

// D5 in Delay Lama Everything: the pitch wheel bends the pitch; it does not
// move the vowel as in the standalone's MIDI input. Delay Lama sings every
// note an octave below its MIDI pitch (DSP/voice.c grain_period uses
// note - 12, as MonkSynth does), so note 57 (A3) sounds at 110 Hz.
void pitch_bend_is_pitch() {
    RenderResult a3;
    RenderResult g3;
    RenderResult g3_bent;
    RenderResult g3_wide;
    const std::string clean = "set echo_amount 0\nset vibrato 0\n";
    render(clean + "on 57 100\nwait 600\n", a3);
    render(clean + "on 55 100\nwait 600\n", g3);
    render(clean + "bend 16383\non 55 100\nwait 600\n", g3_bent);
    render(clean + "bendrange 12\nbend 12288\non 55 100\nwait 600\n", g3_wide);
    const double a3_hz = pitch_hz(a3, 300);
    const double g3_hz = pitch_hz(g3, 300);
    const double bent_hz = pitch_hz(g3_bent, 300);
    const double wide_hz = pitch_hz(g3_wide, 300);
    std::printf("voice_render: note 57 %.1f Hz, 55 %.1f Hz, 55+2 %.1f Hz, 55+6 %.1f Hz\n",
                a3_hz, g3_hz, bent_hz, wide_hz);
    check(std::fabs(a3_hz / 110.0 - 1.0) < 0.03, "note 57 sings near 110 Hz");
    check(std::fabs(bent_hz / a3_hz - 1.0) < 0.01, "note 55 bent up 2 semitones sings note 57");
    check(std::fabs(g3_hz / a3_hz - 1.0) > 0.08, "unbent note 55 is a tone lower");
    // Half way up a 12-semitone range: 6 semitones, C#4.
    check(std::fabs(wide_hz / (g3_hz * std::pow(2.0, 0.5)) - 1.0) < 0.02,
          "bendrange widens the bend");

    Choir choir(2);
    std::string error;
    Command command;
    parse_line("on 55 100", command, error);
    choir.apply(command, error);
    parse_line("bend 0", command, error);
    choir.apply(command, error);
    check(std::fabs(choir.monk_vowel(0) - 0.5f) < 1e-6, "a bend leaves the vowel alone");
    check(choir.monk_singing(0) && !choir.monk_singing(1), "one note wakes one monk");
}

void script_errors() {
    check(render_error("on 60 100\nset bogus 1\n").rfind("line 2: unknown set key", 0) == 0,
          "an unknown set key names its line");
    check(render_error("set glide 2\n").rfind("line 1: set glide takes 0 to 1", 0) == 0,
          "an out-of-range value is rejected");
    check(render_error("set unison_voices 2.5\n").rfind("line 1:", 0) == 0,
          "unison_voices must be whole");
    check(render_error("hello 2\n").rfind("line 1: protocol version 2", 0) == 0,
          "another protocol version is rejected");
    check(render_error("\n# fine\noff\n").rfind("line 3: usage: off", 0) == 0,
          "parse errors name their line");

    RenderResult result;
    render("on 60 100\nwait 100\nquit\nwait 5000\n", result);
    check(result.pcm.size() == 2 * frame_at(100.0), "quit ends the script");
}


// Profile mode (protocol 1, the articulator; Delay Lama Everything phase 4b).
const char kProfileHead[] = "hello 1\nset articulator 1\ncc 7 127\nset vibrato.rate 6\n";

// Normalised autocorrelation pitch of the left channel over a short window
// [from_ms, from_ms + window_ms), searching low_hz..high_hz. The first peak
// within 0.03 of the best one, so octave errors go downward.
double short_pitch_hz(const RenderResult &result, double from_ms, double window_ms,
                      double low_hz, double high_hz) {
    const size_t start = frame_at(from_ms);
    const size_t length = frame_at(window_ms);
    const size_t min_lag = static_cast<size_t>(kSampleRate / high_hz);
    const size_t max_lag = static_cast<size_t>(kSampleRate / low_hz);
    if ((start + length + max_lag + 1) * 2 > result.pcm.size())
        return 0.0;
    const auto sample = [&](size_t frame) { return result.pcm[frame * 2] / 32768.0; };
    std::vector<double> score(max_lag + 2, -1.0);
    for (size_t lag = min_lag; lag <= max_lag + 1; ++lag) {
        double cross = 0.0;
        double power_a = 0.0;
        double power_b = 0.0;
        for (size_t index = start; index < start + length; ++index) {
            cross += sample(index) * sample(index + lag);
            power_a += sample(index) * sample(index);
            power_b += sample(index + lag) * sample(index + lag);
        }
        score[lag] = cross / std::sqrt(power_a * power_b + 1e-30);
    }
    double best = -1.0;
    for (size_t lag = min_lag; lag <= max_lag; ++lag)
        best = std::max(best, score[lag]);
    if (best < 0.5)
        return 0.0;
    for (size_t lag = min_lag + 1; lag <= max_lag; ++lag) {
        if (score[lag] >= best - 0.03 && score[lag] >= score[lag - 1] &&
            score[lag] >= score[lag + 1]) {
            const double a = score[lag - 1];
            const double b = score[lag];
            const double c = score[lag + 1];
            const double den = a - 2.0 * b + c;
            const double frac = std::fabs(den) > 1e-12 ? 0.5 * (a - c) / den : 0.0;
            return kSampleRate / (static_cast<double>(lag) + frac);
        }
    }
    return 0.0;
}

// Amplitude of the left channel at `hz` over [from_ms, from_ms + window_ms),
// Hann-windowed (one DFT bin).
double tone_level(const RenderResult &result, double hz, double from_ms, double window_ms) {
    const size_t start = frame_at(from_ms);
    const size_t length = frame_at(window_ms);
    if ((start + length) * 2 > result.pcm.size() || length < 2)
        return 0.0;
    const double pi = 3.14159265358979323846;
    double re = 0.0;
    double im = 0.0;
    for (size_t index = 0; index < length; ++index) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * pi * static_cast<double>(index) /
                                              static_cast<double>(length - 1));
        const double x = result.pcm[(start + index) * 2] / 32768.0 * w;
        const double phase = 2.0 * pi * hz * static_cast<double>(index) / kSampleRate;
        re += x * std::cos(phase);
        im -= x * std::sin(phase);
    }
    return std::sqrt(re * re + im * im) / static_cast<double>(length);
}

double note_hz(double note) { return 440.0 * std::pow(2.0, (note - 69.0) / 12.0); }

double db(double ratio) { return 20.0 * std::log10(ratio + 1e-30); }

void articulator_basics() {
    // The same script renders the same samples.
    const std::string script =
        std::string(kProfileHead) +
        "set onset.vowel 0.8\nset onset.time 60\nset body.decay 400\n"
        "set body.sustain 0.3\nset ending.vowel 0\nset ending.release 80\n"
        "on 60 100 300\nwait 300\noff 60\non 64 90\nwait 120\non 67 80 -1\n"
        "wait 200\nnext onset.scoop -2\nnext onset.scoop_time 80\n"
        "off 64\noff 67\non 62 110\nwait 250\nallnotesoff\nwait 100\n";
    RenderResult first;
    RenderResult second;
    render(script, first, 3, 200.0);
    render(script, second, 3, 200.0);
    check(first.pcm == second.pcm, "profile mode renders deterministically");
    check(first.peak > 0.01 && first.clipped == 0, "profile mode sings without clipping");

    // D14: the written pitch. Note 57 (A3) sounds at 220 Hz in profile mode
    // (110 Hz in plain mode).
    RenderResult a3;
    render(std::string(kProfileHead) + "on 57 100\nwait 600\n", a3, 1);
    const double hz = pitch_hz(a3, 300);
    std::printf("voice_render: profile note 57 %.1f Hz\n", hz);
    check(std::fabs(hz / 220.0 - 1.0) < 0.03, "profile mode sings the written pitch");

    // A note's first values are in place at its note-on: after a bright note,
    // a note on a dark vowel starts dark (no bright first grain).
    RenderResult dark;
    render(std::string(kProfileHead) +
               "set nucleus.vowel 1\nset nucleus.pitch_track 0\n"
               "on 60 100 200\nwait 200\noff 60\nwait 100\nset profile.begin 1\n"
               "set nucleus.vowel 0\nset profile.apply 1\non 60 100\nwait 300\n",
           dark, 1);
    // Harmonic 8 of C4 (2093 Hz) sits on F2 of "i" and far above "u".
    const double bright = tone_level(dark, note_hz(60) * 8, 300.0, 10.0);
    const double body = tone_level(dark, note_hz(60) * 8, 450.0, 10.0);
    std::printf("voice_render: h8 in the first 10 ms %.1f dB vs the body\n", db(bright / body));
    check(bright < body * 2.0, "a new note starts on its own vowel");

    check(render_error("set articulator 1\nset vowel 0.5\n").rfind("line 2: set vowel", 0) == 0,
          "plain keys are refused in profile mode");
    check(render_error("set articulator 1\nset onset.time 900\n")
                  .rfind("line 2: set onset.time takes", 0) == 0,
          "profile keys check their range");
    check(render_error("next onset.vowel 0.5\n").rfind("line 1: next needs profile mode", 0) == 0,
          "next needs profile mode");
    RenderResult nulls;
    render(std::string(kProfileHead) + "set onset.vowel -1\nset ending.vowel_time -1\n", nulls, 1);
}

// The onset glitches of the phase 4a listening test (Delay Lama Everything
// renders/phase4b/diag): a vowel gliding at full level across a harmonic
// flares it (trumpet E5: +18 to +24 dB). onset.mask raises the level with
// the move instead.
double onset_flare(double mask) {
    RenderResult r;
    char script[640];
    std::snprintf(script, sizeof(script),
                  "%sset voice.size 0.6\nset nucleus.vowel 0.6\nset nucleus.pitch_track 0\n"
                  "set onset.vowel 0.75\nset onset.time 25\nset onset.mask %g\n"
                  "set body.breath 0.45\non 76 100\nwait 400\n",
                  kProfileHead, mask);
    render(script, r, 1);
    const double hz = note_hz(76) * 3; // E5's third harmonic, 1977 Hz
    double onset = 0.0;
    for (double ms = 0.0; ms < 60.0; ms += 2.0)
        onset = std::max(onset, tone_level(r, hz, ms, 10.0));
    double body = 0.0;
    for (double ms = 200.0; ms < 380.0; ms += 10.0)
        body = std::max(body, tone_level(r, hz, ms, 10.0));
    return db(onset / body);
}

void articulator_onset_flare() {
    const double unmasked = onset_flare(0.0);
    const double masked = onset_flare(2.0);
    std::printf("voice_render: E5 h3 onset flare %.1f dB unmasked, %.1f dB masked\n", unmasked,
                masked);
    check(unmasked > 8.0, "an unmasked onset vowel glide flares a harmonic (the mechanism)");
    check(masked < 8.0, "onset.mask 2 keeps the onset flare under 8 dB");
}

// Phase 4b round 3 (D33). onset.bandwidth widens the DSP's formants while the
// onset vowel moves, then returns them exactly to the DSP's own; after that
// the sound is the same sample for sample. onset.after_rest keeps the move
// for notes that start after the channel rested.
void articulator_bandwidth_and_rest() {
    const std::string doo = std::string(kProfileHead) +
                            "set nucleus.vowel 0.12\nset nucleus.pitch_track 0\n"
                            "set onset.vowel 0.55\nset onset.time 40\n";
    RenderResult narrow;
    RenderResult wide;
    render(doo + "on 33 100\nwait 600\n", narrow, 1);
    render(doo + "set onset.bandwidth 4\non 33 100\nwait 600\n", wide, 1);
    const auto slice = [](const RenderResult &r, double from_ms, double to_ms) {
        return std::vector<int16_t>(r.pcm.begin() + 2 * frame_at(from_ms),
                                    r.pcm.begin() + 2 * frame_at(to_ms));
    };
    check(slice(narrow, 0, 60) != slice(wide, 0, 60), "onset.bandwidth changes the onset");
    // The DSP's 5 ms gain smoothing lands within a float step of the old
    // level, not on it, after the level made up for the wide formants.
    const std::vector<int16_t> a = slice(narrow, 200, 600);
    const std::vector<int16_t> b = slice(wide, 200, 600);
    int largest = a.size() == b.size() ? 0 : 99;
    for (size_t index = 0; largest < 99 && index < a.size(); ++index)
        largest = std::max(largest, std::abs(a[index] - b[index]));
    check(largest <= 1, "after the onset the formants are the DSP's own again (within 1 LSB)");
    std::printf("voice_render: A1 'doo' onset, first 60 ms energy %.1f dB with bandwidth 4\n",
                10.0 * std::log10(energy(wide, 0, 60) / energy(narrow, 0, 60)));

    // C4's 8th harmonic (2093 Hz) sits on F2 of "i" (onset) and far above the
    // nucleus 0.2: bright at a start that moves, dark at a steady one.
    RenderResult r;
    render(std::string(kProfileHead) +
               "set nucleus.vowel 0.2\nset nucleus.pitch_track 0\nset onset.vowel 1\n"
               "set onset.time 60\nset onset.after_rest 300\nset phrase.mode 3\n"
               "on 60 100 200\nwait 200\noff 60\nwait 100\non 60 100 200\nwait 200\noff 60\n"
               "wait 400\non 60 100 200\nwait 200\noff 60\nwait 100\n",
           r, 1);
    const auto brightness = [&](double onset_ms) {
        const double hz = note_hz(60) * 8;
        return db(tone_level(r, hz, onset_ms + 2.0, 10.0) / tone_level(r, hz, onset_ms + 150.0, 10.0));
    };
    const double first = brightness(0.0);
    const double soon = brightness(300.0);
    const double rested = brightness(900.0);
    std::printf("voice_render: after_rest 300: h8 at the start %.1f / %.1f / %.1f dB vs the body\n",
                first, soon, rested);
    check(first > 10.0 && rested > 10.0, "a note after a rest gets the onset move");
    check(soon < 6.0, "a note 100 ms after the last one starts steady");
}

// Pitch: a legato glide on the DSP glide and scooped re-articulated notes
// never go above their note (beyond the vibrato floor).
void articulator_pitch() {
    RenderResult legato;
    render(std::string(kProfileHead) +
               "set phrase.mode 1\nset phrase.glide 0.15\n"
               "on 57 100\nwait 300\non 64 100\nwait 1\noff 57\nwait 400\n",
           legato, 1);
    double worst = -99.0;
    // A fifth at glide 0.15 (75 st/s) takes about 95 ms; the tracker can't
    // follow the slide itself, so check where it lands.
    for (double ms = 420.0; ms < 660.0; ms += 5.0) {
        const double hz = short_pitch_hz(legato, ms, 25.0, 150.0, 500.0);
        if (hz > 0.0)
            worst = std::max(worst, 12.0 * std::log2(hz / note_hz(64)));
    }
    std::printf("voice_render: legato A3->E4 highest %.2f st from E4\n", worst);
    check(worst > -1.0 && worst < 0.45, "a legato glide does not overshoot");

    RenderResult scoop;
    render(std::string(kProfileHead) +
               "set onset.scoop -1\nset onset.scoop_time 90\nset phrase.mode 2\n"
               "on 64 100 250\nwait 250\noff 64\non 64 100 250\nwait 250\n"
               "off 64\non 62 100\nwait 300\n",
           scoop, 1);
    worst = -99.0;
    for (double ms = 0.0; ms < 760.0; ms += 5.0) {
        if (ms > 470.0 && ms < 530.0)
            continue; // the step from E4 to D4 inside the window
        const double hz = short_pitch_hz(scoop, ms, 25.0, 150.0, 500.0);
        const double target = ms < 500.0 ? note_hz(64) : note_hz(62);
        if (hz > 0.0)
            worst = std::max(worst, 12.0 * std::log2(hz / target));
    }
    std::printf("voice_render: scooped repeats highest %.2f st from the note\n", worst);
    check(worst > -1.0 && worst < 0.45, "scooped and re-articulated notes stay at their pitch");
}

// A re-articulation dips; nothing between two notes is louder than the notes
// (the old "blip" of the previous note before a new one), and the ending
// lets the monk fall silent.
void articulator_no_blips() {
    RenderResult r;
    render(std::string(kProfileHead) +
               "set phrase.mode 2\nset onset.vowel 0.3\nset onset.time 60\n"
               "on 45 100 300\nwait 300\noff 45\nwait 10\non 48 100\nwait 300\noff 48\n"
               "wait 200\n",
           r, 1, 200.0);
    // Low notes are pulse trains, so compare with the note's own loudest 2 ms.
    double note = 0.0;
    for (double ms = 150.0; ms < 278.0; ms += 2.0)
        note = std::max(note, energy(r, ms, ms + 2.0) / 2.0);
    double worst = 0.0;
    for (double ms = 280.0; ms < 330.0; ms += 2.0)
        worst = std::max(worst, energy(r, ms, ms + 2.0) / 2.0);
    std::printf("voice_render: loudest 2 ms between notes %.1f dB vs the note\n",
                10.0 * std::log10(worst / note + 1e-30));
    check(note > 0.0 && worst < note * 1.12, "no blip between two notes");
    check(energy(r, 850, 1000) < 1e-9, "the ending runs out and the monk falls silent");
}

// Pitch deviation (cents) of the component near `hz`, every ms over
// [from_ms, to_ms): the phase advance of a 50 ms Hann-windowed DFT at `hz`,
// taken on absolute time, of both channels summed.
std::vector<double> cents_track(const RenderResult &result, double hz, double from_ms,
                                double to_ms) {
    const double pi = 3.14159265358979323846;
    const size_t length = frame_at(50.0);
    std::vector<double> out;
    double last = 0.0;
    bool have = false;
    for (double ms = from_ms; ms < to_ms; ms += 1.0) {
        const size_t start = frame_at(ms);
        if ((start + length) * 2 > result.pcm.size())
            break;
        double re = 0.0;
        double im = 0.0;
        for (size_t index = 0; index < length; ++index) {
            const double w = 0.5 - 0.5 * std::cos(2.0 * pi * static_cast<double>(index) /
                                                  static_cast<double>(length - 1));
            const size_t frame = start + index;
            const double x = (result.pcm[frame * 2] + result.pcm[frame * 2 + 1]) / 65536.0 * w;
            const double phase = 2.0 * pi * hz * static_cast<double>(frame) / kSampleRate;
            re += x * std::cos(phase);
            im -= x * std::sin(phase);
        }
        const double phase = std::atan2(im, re);
        if (have) {
            double step = phase - last;
            while (step > pi)
                step -= 2.0 * pi;
            while (step < -pi)
                step += 2.0 * pi;
            const double offset_hz = step / (2.0 * pi * 0.001);
            out.push_back(1200.0 * std::log2((hz + offset_hz) / hz));
        }
        last = phase;
        have = true;
    }
    return out;
}

// vibrato.lock (Delay Lama Everything phase 4c): a C4 and a G4 started 300 ms
// apart and held. On their own each monk's vibrato floor (+-0.2 st) runs
// apart and the fifth swings; locked, the second monk takes over the first's
// LFO and the fifth holds.
double fifth_wobble_cents(bool lock) {
    RenderResult r;
    render(std::string(kProfileHead) + "set phrase.mode 3\nset choir.vibrato_var 0.08\n" +
               (lock ? "set vibrato.lock 1\n" : "") +
               "on 60 100\nwait 300\non 67 100\nwait 2700\noff 60\noff 67\nwait 50\n",
           r, 2);
    const std::vector<double> low = cents_track(r, note_hz(60), 700.0, 2700.0);
    const std::vector<double> high = cents_track(r, note_hz(67), 700.0, 2700.0);
    const size_t count = std::min(low.size(), high.size());
    double sum = 0.0;
    double squares = 0.0;
    for (size_t index = 0; index < count; ++index) {
        const double interval = high[index] - low[index];
        sum += interval;
        squares += interval * interval;
    }
    if (count < 1000)
        return -1.0;
    const double mean = sum / static_cast<double>(count);
    return std::sqrt(std::max(0.0, squares / static_cast<double>(count) - mean * mean));
}

void articulator_vibrato_lock() {
    const double apart = fifth_wobble_cents(false);
    const double locked = fifth_wobble_cents(true);
    std::printf("voice_render: fifth C4-G4 wobbles %.1f cents (sd) on its own, %.1f locked\n",
                apart, locked);
    check(apart > 10.0, "monks on their own wobble apart (the mechanism)");
    check(locked >= 0.0 && locked < 6.0, "vibrato.lock keeps a chord's interval steady");
}

// Phase 4c pipes (Delay Lama Everything): voice.formant3 0 silences the DSP's
// third formant, so the harmonic of C5 nearest it (H4, 2093 Hz; F3 of "u" is
// 2240 Hz at voice size 0.5) falls against the fundamental; dyn.gain scales a
// profile's level.
void articulator_formants_and_gain() {
    const auto sing = [](const std::string &keys, RenderResult &r) {
        render(std::string(kProfileHead) +
                   "set nucleus.vowel 0\nset nucleus.pitch_track 0\nset voice.size 0.5\n" + keys +
                   "on 72 100\nwait 800\noff 72\nwait 50\n",
               r, 1);
    };
    RenderResult plain;
    RenderResult no_f3;
    RenderResult louder;
    sing("", plain);
    sing("set voice.formant3 0\n", no_f3);
    sing("set dyn.gain 2\n", louder);
    const double f0 = note_hz(72);
    const auto h4_vs_h1 = [&](const RenderResult &r) {
        return db(tone_level(r, f0 * 4, 400.0, 300.0) / tone_level(r, f0, 400.0, 300.0));
    };
    const double before = h4_vs_h1(plain);
    const double after = h4_vs_h1(no_f3);
    const double gain = db(std::sqrt(energy(louder, 300.0, 750.0) / energy(plain, 300.0, 750.0)));
    std::printf("voice_render: C5 H4 vs H1 %.1f dB, %.1f dB with F3 off; dyn.gain 2 %+.1f dB\n",
                before, after, gain);
    check(after < before - 10.0, "voice.formant3 0 takes F3's harmonic down");
    check(std::fabs(gain - 6.02) < 0.5, "dyn.gain 2 doubles the level");
}

} // namespace

int main() {
    determinism_and_timing();
    silence_and_levels();
    release_tail_keeps_its_level();
    pitch_bend_is_pitch();
    script_errors();
    articulator_basics();
    articulator_onset_flare();
    articulator_bandwidth_and_rest();
    articulator_pitch();
    articulator_no_blips();
    articulator_vibrato_lock();
    articulator_formants_and_gain();
    if (failures != 0) {
        std::fprintf(stderr, "voice_render: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("voice_render: PASS\n");
    return 0;
}
