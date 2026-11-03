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
    check(render_error("hello 1\n").rfind("line 1: protocol version 1", 0) == 0,
          "another protocol version is rejected");
    check(render_error("\n# fine\noff\n").rfind("line 3: usage: off", 0) == 0,
          "parse errors name their line");

    RenderResult result;
    render("on 60 100\nwait 100\nquit\nwait 5000\n", result);
    check(result.pcm.size() == 2 * frame_at(100.0), "quit ends the script");
}

} // namespace

int main() {
    determinism_and_timing();
    silence_and_levels();
    pitch_bend_is_pitch();
    script_errors();
    if (failures != 0) {
        std::fprintf(stderr, "voice_render: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("voice_render: PASS\n");
    return 0;
}
