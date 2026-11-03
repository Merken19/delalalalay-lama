#include "voice_choir.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace delay_lama::voice {
namespace {

// Headroom for the choir. The standalone's loudness (volume 0.28 x level
// 0.75) already peaks near full scale with one monk, so a velocity-100 note
// at the GM default volume (CC7 100) is about 16 dB below it here (peaks
// near -16 dBFS on middle C). Four monks at velocity 127 with CC7 127 then
// just stay under full scale. `set gain` and `set volume` raise it.
constexpr float kDefaultVolume = 0.09f;
constexpr uint32_t kMixFrames = 512;

float clamp01(double value) {
    return value > 0.0 ? (value < 1.0 ? static_cast<float>(value) : 1.0f) : 0.0f;
}

struct KeyRange {
    const char *key;
    double low;
    double high;
};

// `set` keys: the standalone's sound settings (named after its INI keys)
// plus vowel and the channel gain.
constexpr KeyRange kKeys[] = {
    {"gain", 0.0, 4.0},          {"volume", 0.0, 1.0},       {"glide", 0.0, 1.0},
    {"voice", 0.0, 1.0},         {"vowel", 0.0, 1.0},        {"vibrato", 0.0, 1.0},
    {"vibrato_rate", 0.0, 1.0},  {"breath", 0.0, 1.0},       {"echo_amount", 0.0, 1.0},
    {"echo_time", 0.0, 1.0},     {"echo_feedback", 0.0, 0.95}, {"attack", 0.0, 10.0},
    {"decay", 0.0, 10.0},        {"sustain", 0.0, 1.0},      {"release", 0.0, 10.0},
    {"unison_voices", 1.0, 10.0}, {"unison_detune", 0.0, 100.0}, {"unison_spread", 0.0, 1.0},
};

} // namespace

Choir::Choir(int monks) : allocator_(monks) {
    cc_[7] = 100;
    cc_[11] = 127;
    for (int monk = 0; monk < allocator_.monks(); ++monk) {
        DelayLamaHandle *handle = dl_create_without_midi(kSampleRate);
        if (!handle) {
            ok_ = false;
            continue;
        }
        handles_.push_back(handle);
        // The standalone's default sound (app.h SoundSettings), louder volume.
        dl_set_volume(handle, kDefaultVolume);
        dl_set_glide(handle, 0.5f);
        dl_set_voice(handle, 0.5f);
        dl_set_vowel(handle, 0.5f);
        dl_set_vibrato(handle, 0.15f);
        dl_set_vibrato_rate(handle, 0.5f);
        dl_set_breath(handle, 0.5f);
        dl_set_delay(handle, 0.42f);
        dl_set_delay_time(handle, 0.5f);
        dl_set_delay_feedback(handle, 0.5f);
        dl_set_level(handle, level_for(100));
    }
    apply_envelope();
    apply_unison();
}

Choir::~Choir() {
    for (DelayLamaHandle *handle : handles_)
        dl_destroy(handle);
}

bool Choir::apply(const Command &command, std::string &error) {
    switch (command.type) {
    case CommandType::NoteOn:
        note_on(command.a, command.b);
        break;
    case CommandType::NoteOff:
        note_off(command.a);
        break;
    case CommandType::Bend:
        bend_ = command.a;
        apply_bend();
        break;
    case CommandType::BendRange:
        bend_range_ = command.value;
        apply_bend();
        break;
    case CommandType::Cc:
        control_change(command.a, command.b);
        break;
    case CommandType::Program:
        // Kept for the imitation profiles (Delay Lama Everything phase 4).
        bank_ = command.a;
        program_ = command.b;
        break;
    case CommandType::AllNotesOff:
        all_notes_off();
        break;
    case CommandType::Set:
        return set(command.key, command.value, error);
    case CommandType::Hello:
    case CommandType::Wait:
    case CommandType::Quit:
        break;
    }
    return true;
}

void Choir::render(float *left, float *right, uint32_t frames) {
    float monk_left[kMixFrames];
    float monk_right[kMixFrames];
    while (frames > 0) {
        const uint32_t count = std::min(frames, kMixFrames);
        std::fill(left, left + count, 0.0f);
        std::fill(right, right + count, 0.0f);
        for (DelayLamaHandle *handle : handles_) {
            dl_process(handle, monk_left, monk_right, count);
            for (uint32_t frame = 0; frame < count; ++frame) {
                left[frame] += monk_left[frame];
                right[frame] += monk_right[frame];
            }
        }
        left += count;
        right += count;
        frames -= count;
    }
}

void Choir::render_callback(void *choir, float *left, float *right, uint32_t frames) {
    static_cast<Choir *>(choir)->render(left, right, frames);
}

bool Choir::monk_singing(int monk) const {
    if (monk < 0 || monk >= monks())
        return false;
    return dl_is_active(handles_[static_cast<size_t>(monk)]) != 0;
}

float Choir::monk_vowel(int monk) const {
    if (monk < 0 || monk >= monks())
        return 0.5f;
    return dl_get_vowel(handles_[static_cast<size_t>(monk)]);
}

void Choir::note_on(int note, int velocity) {
    if (velocity == 0) {
        note_off(note);
        return;
    }
    allocator_.note_on(
        note, velocity,
        [this](int monk) { return dl_is_active(handles_[static_cast<size_t>(monk)]) != 0; },
        actions_);
    run(actions_);
}

void Choir::note_off(int note) {
    allocator_.note_off(note, actions_);
    run(actions_);
}

// Acts on volume, expression, sustain and the channel-mode resets. Every
// value is kept, for the imitation profiles to use later.
void Choir::control_change(int number, int value) {
    cc_[static_cast<size_t>(number)] = value;
    switch (number) {
    case 7:
    case 11:
        update_levels();
        break;
    case 64:
        allocator_.set_sustain(value >= 64, actions_);
        run(actions_);
        break;
    case 120: // all sound off
    case 123: // all notes off
        all_notes_off();
        break;
    case 121: // reset all controllers (GM RP-015: not volume)
        cc_[1] = 0;
        cc_[11] = 127;
        cc_[64] = 0;
        allocator_.set_sustain(false, actions_);
        run(actions_);
        bend_ = 8192;
        apply_bend();
        update_levels();
        break;
    default:
        break;
    }
}

void Choir::all_notes_off() {
    allocator_.all_notes_off(actions_);
    run(actions_);
    for (DelayLamaHandle *handle : handles_)
        dl_all_notes_off(handle);
}

void Choir::run(std::vector<MonkAction> &actions) {
    for (const MonkAction &action : actions) {
        DelayLamaHandle *handle = handles_[static_cast<size_t>(action.monk)];
        const uint8_t note = static_cast<uint8_t>(action.note);
        if (action.kind == MonkAction::Kind::On) {
            dl_set_level(handle, level_for(action.velocity));
            dl_note_on(handle, note, static_cast<float>(action.velocity) / 127.0f);
        } else {
            dl_note_off(handle, note);
        }
    }
    actions.clear();
}

// GM curves (40 log10 of each value over 127, in dB) for velocity, CC7 and
// CC11, times the channel gain. The DSP itself ignores velocity.
float Choir::level_for(int velocity) const {
    const double v = velocity / 127.0;
    const double volume = cc_[7] / 127.0;
    const double expression = cc_[11] / 127.0;
    return clamp01(gain_ * v * v * volume * volume * expression * expression);
}

// Monks without a note keep their last velocity, so release tails follow
// volume changes too.
void Choir::update_levels() {
    for (int monk = 0; monk < monks(); ++monk)
        dl_set_level(handles_[static_cast<size_t>(monk)], level_for(allocator_.velocity_of(monk)));
}

void Choir::apply_bend() {
    const double offset = bend_ - 8192;
    const double semitones = offset / (offset >= 0 ? 8191.0 : 8192.0) * bend_range_;
    for (DelayLamaHandle *handle : handles_)
        dl_set_pitch_bend(handle, static_cast<float>(semitones));
}

void Choir::apply_envelope() {
    for (DelayLamaHandle *handle : handles_)
        dl_set_envelope(handle, attack_, decay_, sustain_level_, release_);
}

void Choir::apply_unison() {
    for (DelayLamaHandle *handle : handles_)
        dl_set_unison(handle, unison_voices_, unison_detune_, unison_spread_);
}

bool Choir::set(const std::string &key, double value, std::string &error) {
    const KeyRange *range = nullptr;
    for (const KeyRange &candidate : kKeys) {
        if (key == candidate.key)
            range = &candidate;
    }
    if (!range) {
        error = "unknown set key " + key;
        return false;
    }
    if (value < range->low || value > range->high ||
        (key == "unison_voices" && value != std::floor(value))) {
        char text[128];
        snprintf(text, sizeof(text), "set %s takes %g to %g", range->key, range->low,
                 range->high);
        error = text;
        return false;
    }

    const float v = static_cast<float>(value);
    const auto each = [this](void (*setter)(DelayLamaHandle *, float), float x) {
        for (DelayLamaHandle *handle : handles_)
            setter(handle, x);
    };
    if (key == "gain") {
        gain_ = value;
        update_levels();
    } else if (key == "volume") {
        each(dl_set_volume, v);
    } else if (key == "glide") {
        each(dl_set_glide, v);
    } else if (key == "voice") {
        each(dl_set_voice, v);
    } else if (key == "vowel") {
        each(dl_set_vowel, v);
    } else if (key == "vibrato") {
        each(dl_set_vibrato, v);
    } else if (key == "vibrato_rate") {
        each(dl_set_vibrato_rate, v);
    } else if (key == "breath") {
        each(dl_set_breath, v);
    } else if (key == "echo_amount") {
        each(dl_set_delay, v);
    } else if (key == "echo_time") {
        each(dl_set_delay_time, v);
    } else if (key == "echo_feedback") {
        each(dl_set_delay_feedback, v);
    } else if (key == "attack") {
        attack_ = v;
        apply_envelope();
    } else if (key == "decay") {
        decay_ = v;
        apply_envelope();
    } else if (key == "sustain") {
        sustain_level_ = v;
        apply_envelope();
    } else if (key == "release") {
        release_ = v;
        apply_envelope();
    } else if (key == "unison_voices") {
        unison_voices_ = static_cast<int>(value);
        apply_unison();
    } else if (key == "unison_detune") {
        unison_detune_ = v;
        apply_unison();
    } else {
        unison_spread_ = v;
        apply_unison();
    }
    return true;
}

} // namespace delay_lama::voice
