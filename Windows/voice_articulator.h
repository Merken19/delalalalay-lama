#pragma once

#include "voice_bridge.h"

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct MonkDelayHolder;

namespace delay_lama::voice {

// The articulator of DelayLamaVoice.exe (Delay Lama Everything phase 4b; the
// spec is docs/IMITATION.md, decisions D12-D25). In profile mode it owns each
// monk's vowel, level, breath, pitch offset, vibrato and voice size, and
// computes them every 1 ms control tick from the channel's imitation profile:
// each note runs onset, nucleus, body and ending, and notes on one monk join
// as new, re-articulated or legato.
//
// The DSP envelope and the synths' own echo are off in profile mode: notes
// are shaped through the level, and the choir mix gets one helper-owned echo.
// The articulator does its own voice allocation (connected notes stay on one
// monk), so the phase 1 allocator is only used in plain mode.
//
// Threads: every call is made under the choir's lock.

// Profile keys, `section.param` as in IMITATION.md section 7. Null values
// (follow the nucleus, or the release time) are -1.
enum class Key {
#define DL_KEY(id, name, low, high, def, nullable) id,
#include "voice_profile_keys.inc"
#undef DL_KEY
    Count
};

constexpr int kKeyCount = static_cast<int>(Key::Count);

struct KeyInfo {
    const char *name;
    double low;
    double high;
    double def;
    bool nullable;
};

const KeyInfo &key_info(Key key);
// The key named `name`, or false.
bool find_key(const std::string &name, Key &key);

struct Profile {
    std::array<double, kKeyCount> v;
    Profile();
    double operator[](Key key) const { return v[static_cast<size_t>(key)]; }
    double &operator[](Key key) { return v[static_cast<size_t>(key)]; }
};

class Articulator {
public:
    // One synth handle per monk; the articulator does not own them.
    explicit Articulator(std::vector<DelayLamaHandle *> handles);
    ~Articulator();
    Articulator(const Articulator &) = delete;
    Articulator &operator=(const Articulator &) = delete;

    // Profile mode on: synths' envelope and echo off, seeds and unison set,
    // every monk silent.
    void start();
    // Back to plain mode: every monk silent (the choir restores its settings).
    void stop();

    void note_on(int note, int velocity, double duration_ms);
    void note_off(int note);
    void sustain(bool down);
    void all_notes_off();

    // Channel state from the choir: bend in semitones, CC7, CC11, CC1 (0-127)
    // and the channel gain.
    void set_channel(double bend_semitones, int volume, int expression, int modwheel,
                     double gain);

    // Profile and channel keys (IMITATION.md sections 7 and 13). False, with
    // `error`, for an unknown key or a value out of range.
    bool set(const std::string &key, double value, std::string &error);
    // Whether `key` is one of the articulator's keys.
    static bool handles_key(const std::string &key);
    // `next <key> <value>`: profile override for the next note only.
    bool next(const std::string &key, double value, std::string &error);

    // Advances one control tick (1 ms) and sends every monk's values to its
    // synth. Call before rendering each 1 ms of audio.
    void tick();
    static constexpr double kTickMs = 1.0;

    // Mixes the monks' outputs: equal-power pan per monk, then the echo.
    // `monk_l`/`monk_r` hold `frames` samples per monk, monk after monk.
    void mix(const float *monk_l, const float *monk_r, int frames, float *left, float *right);

    bool singing(int monk) const;
    float vowel(int monk) const;
    // Closed-lips mouth (D16): onset or ending lips, a dark dip, or a very low
    // level during a dip or an ending.
    bool lips_closed(int monk) const;
    double now_ms() const { return now_; }
    const Profile &profile() const { return profile_; }

private:
    enum class State { Idle, Dip, Sounding, Ending };
    enum class Join { New, Rearticulate, Legato };

    struct Note {
        // Resolved per note at its onset (the snapshot, section 4).
        Profile p;
        int note = -1;          // written MIDI note
        int velocity = 0;
        double sounding = 60.0; // sounding MIDI note (written + shifts)
        int dsp_note = 72;      // the DSP sings an octave below its note (D14)
        double fine = 0.0;      // fractional shift, sent through the bend
        double nucleus = 0.5;
        double onset_vowel = 0.5;
        double body_vowel = -1.0;
        double peak = 1.0;
        double k = 1.0;         // fast-note compression (section 6.8)
        double burst = -1.0;
        double bandwidth = 1.0; // onset.bandwidth, or 1 when the onset vowel holds
        double base_level = 1.0;
        double duration = -1.0;
    };

    struct Monk {
        State state = State::Idle;
        Join join = Join::New;
        Note n;
        bool key_down = false;
        bool sustained = false;
        double start = -1e9;       // onset (the dip's start when re-articulated)
        double sound_start = -1e9; // the new note's pitch starts here
        double off_time = -1e9;    // last note-off (key up)
        double end_start = 0.0;
        double end_level = 0.0;
        double end_vowel = 0.5;
        double end_release = 20.0;
        bool quick_end = false;
        // Where the previous note left off (legato and dips start from here).
        double from_vowel = 0.5;
        double from_shape = 0.0;
        double glide_from = 0.0; // glide_mode 1: start offset in semitones
        double from_offset = 0.0; // the old note's bend offset (kept in a dip)
        double from_bandwidth = 1.0;
        double end_bandwidth = 1.0;
        // Last values sent.
        double vowel = 0.5;
        double shape = 0.0;
        double breath = 0.75;
        double offset = 0.0;
        bool lips = false;
        double bandwidth = 1.0; // formant bandwidth factor last sent
        double voice_offset = 0.0;
        double rate_factor = 1.0;
    };

    void begin_note(int monk, int note, int velocity, double duration, Join join);
    void start_sound(int monk);
    void start_ending(int monk, bool quick);
    void update(int monk);
    double base_level(const Note &n) const;
    double ioi(int monk) const;
    int pick_monk(int note, Join &join) const;
    // How long the channel was silent before this note (onset.after_rest);
    // notes of a chord that starts after a rest share its rest.
    double rest_before_note();
    // Notes the time when the last held or sustained note was released.
    void note_released();
    void send_bandwidth(int monk, double bandwidth);
    void apply_choir();
    void apply_unison();
    Profile &target_profile() { return staging_ ? staged_ : profile_; }

    std::vector<DelayLamaHandle *> handles_;
    std::vector<Monk> monks_;
    Profile profile_;
    Profile staged_;
    bool staging_ = false;
    std::vector<std::pair<Key, double>> next_;
    bool sustain_ = false;
    double now_ = 0.0;
    double quiet_since_ = -1e9; // the channel's last moment with no note held
    double phrase_start_ = -1e9;
    double phrase_rest_ = 0.0;

    // Channel state.
    double bend_ = 0.0;
    int volume_ = 100;
    int expression_ = 127;
    int modwheel_ = 0;
    double gain_ = 1.0;
    // Channel keys.
    double exaggerate_ = 1.0;
    double shift_ = 0.0;
    double voice_var_ = 0.03;
    double vibrato_var_ = 0.08;
    bool seed_ = true;
    double pan_ = 0.5;
    double echo_ = 0.0;
    double echo_time_ = 0.5;
    double echo_feedback_ = 0.5;
    double closed_below_ = 0.15;
    std::unique_ptr<MonkDelayHolder> delay_;
    std::vector<float> scratch_;
};

} // namespace delay_lama::voice
