#pragma once

#include "voice_allocator.h"
#include "voice_bridge.h"
#include "voice_protocol.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace delay_lama::voice {

constexpr float kSampleRate = 48000.0f;
constexpr int kDefaultMonks = 4;

// One MIDI channel sung by a choir of monks: a synth per monk (bridge handles
// with no MIDI input), the voice allocator, and the channel state that
// applies to every monk (pitch bend, volume, expression, sustain).
//
// Threads: apply() and the setters run on one thread (the command reader),
// render() on the audio thread, and the monk_* queries on the window thread.
// The bridge handles lock themselves; the rest is only touched by apply().
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
    const VoiceAllocator &allocator() const { return allocator_; }

private:
    void note_on(int note, int velocity);
    void note_off(int note);
    void control_change(int number, int value);
    void all_notes_off();
    void run(std::vector<MonkAction> &actions);
    float level_for(int velocity) const;
    void update_levels();
    void apply_bend();
    void apply_envelope();
    void apply_unison();
    bool set(const std::string &key, double value, std::string &error);

    std::vector<DelayLamaHandle *> handles_;
    VoiceAllocator allocator_;
    std::vector<MonkAction> actions_;
    bool ok_ = true;

    std::array<int, 128> cc_ = {};
    int bend_ = 8192;
    double bend_range_ = 2.0;
    int bank_ = 0;
    int program_ = 0;
    double gain_ = 1.0;
    float attack_ = 0.0f;
    float decay_ = 0.0f;
    float sustain_level_ = 1.0f;
    float release_ = 0.0f;
    int unison_voices_ = 1;
    float unison_detune_ = 0.0f;
    float unison_spread_ = 0.0f;
};

} // namespace delay_lama::voice
