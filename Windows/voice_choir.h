#pragma once

#include "voice_allocator.h"
#include "voice_articulator.h"
#include "voice_bridge.h"
#include "voice_protocol.h"

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace delay_lama::voice {

constexpr float kSampleRate = 48000.0f;
constexpr int kDefaultMonks = 4;

// One MIDI channel sung by a choir of monks: a synth per monk (bridge handles
// with no MIDI input), the voice allocator, and the channel state that
// applies to every monk (pitch bend, volume, expression, sustain).
//
// Two modes: plain (protocol 0's sound, as in phase 1) and profile mode
// (`set articulator 1`), where the articulator shapes every note from the
// channel's imitation profile (Delay Lama Everything phase 4b).
//
// Threads: apply() runs on the command reader, render() on the audio thread,
// and the monk_* queries on the window thread; all take the choir's lock.
class Choir {
public:
    explicit Choir(int monks);
    ~Choir();
    Choir(const Choir &) = delete;
    Choir &operator=(const Choir &) = delete;

    // False if a synth could not be created.
    bool ok() const { return ok_; }
    int monks() const { return static_cast<int>(handles_.size()); }

    // Applies one command. hello, wait and quit belong to the caller and are
    // ignored here. False, with `error` set, for a bad `set`.
    bool apply(const Command &command, std::string &error);

    // Mixes every monk into left and right (overwritten).
    void render(float *left, float *right, uint32_t frames);
    static void render_callback(void *choir, float *left, float *right, uint32_t frames);

    bool monk_singing(int monk) const;
    float monk_vowel(int monk) const;
    // Closed lips (profile mode only, D16).
    bool monk_lips_closed(int monk) const;
    bool profile_mode() const { return profile_mode_; }
    // Whether the monk window should be shown (`set window.visible`, default
    // 1). The router hides it while the user listens to the synthesizer
    // instead of the monks (Delay Lama Everything D60); the sound is the
    // router's `gain`, not this.
    bool window_visible() const;
    const VoiceAllocator &allocator() const { return allocator_; }

private:
    void note_on(int note, int velocity);
    void note_off(int note);
    void control_change(int number, int value);
    void all_notes_off();
    void run(std::vector<MonkAction> &actions);
    float level_for(int velocity) const;
    void set_level(int monk, float level);
    void update_levels();
    void apply_bend();
    void apply_envelope();
    void apply_unison();
    bool set(const std::string &key, double value, std::string &error);
    void set_profile_mode(bool on);
    void update_channel();
    double level_headroom() const;
    void render_plain(float *left, float *right, uint32_t frames);
    void render_profile(float *left, float *right, uint32_t frames);

    mutable std::recursive_mutex mutex_;
    std::vector<DelayLamaHandle *> handles_;
    std::unique_ptr<Articulator> articulator_;
    bool profile_mode_ = false;
    bool window_visible_ = true;
    uint32_t tick_phase_ = 0; // frames rendered since the last control tick
    std::vector<float> monk_l_;
    std::vector<float> monk_r_;
    // Plain-mode sound settings, restored when profile mode ends.
    float glide_ = 0.5f;
    float voice_ = 0.5f;
    float vowel_ = 0.5f;
    float vibrato_ = 0.15f;
    float vibrato_rate_ = 0.5f;
    float breath_ = 0.5f;
    float echo_amount_ = 0.42f;
    VoiceAllocator allocator_;
    std::vector<MonkAction> actions_;
    bool ok_ = true;
    // Per monk: the level last given to its synth, and whether it has no
    // note (then its release tail may only get quieter).
    std::vector<float> levels_;
    std::vector<bool> released_;

    std::array<int, 128> cc_ = {};
    int bend_ = 8192;
    double bend_range_ = 2.0;
    int bank_ = 0;
    int program_ = 0;
    double gain_ = 1.0;
    float volume_ = 0.09f; // the DSP volume (`set volume`); kDefaultVolume
    float attack_ = 0.0f;
    float decay_ = 0.0f;
    float sustain_level_ = 1.0f;
    float release_ = 0.0f;
    int unison_voices_ = 1;
    float unison_detune_ = 0.0f;
    float unison_spread_ = 0.0f;
};

} // namespace delay_lama::voice
