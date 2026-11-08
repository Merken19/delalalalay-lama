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
    }
    articulator_ = std::make_unique<Articulator>(handles_);
    levels_.assign(handles_.size(), 0.0f);
    released_.assign(handles_.size(), true);
    for (int monk = 0; monk < static_cast<int>(handles_.size()); ++monk)
        set_level(monk, level_for(100));
    apply_envelope();
    apply_unison();
}

Choir::~Choir() {
    for (DelayLamaHandle *handle : handles_)
        dl_destroy(handle);
}

bool Choir::apply(const Command &command, std::string &error) {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    switch (command.type) {
    case CommandType::NoteOn:
        if (profile_mode_)
            articulator_->note_on(command.a, command.b, command.value);
        else
            note_on(command.a, command.b);
        break;
    case CommandType::NoteOff:
        if (profile_mode_)
            articulator_->note_off(command.a);
        else
            note_off(command.a);
        break;
    case CommandType::Next:
        if (!profile_mode_) {
            error = "next needs profile mode (set articulator 1)";
            return false;
        }
        return articulator_->next(command.key, command.value, error);
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
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    if (profile_mode_)
        render_profile(left, right, frames);
    else
        render_plain(left, right, frames);
}

// Profile mode: 1 ms sub-blocks, each followed by the articulator's control
// tick (D20). Commands land between sub-blocks (at the exact sample in
// --render), so a note's first values are in place for its first sample.
void Choir::render_profile(float *left, float *right, uint32_t frames) {
    constexpr uint32_t kTickFrames = static_cast<uint32_t>(kSampleRate / 1000.0f);
    const size_t count = handles_.size();
    monk_l_.resize(count * kTickFrames);
    monk_r_.resize(count * kTickFrames);
    while (frames > 0) {
        const uint32_t n = std::min(frames, kTickFrames - tick_phase_);
        std::fill(left, left + n, 0.0f);
        std::fill(right, right + n, 0.0f);
        for (size_t monk = 0; monk < count; ++monk)
            dl_process(handles_[monk], monk_l_.data() + monk * n, monk_r_.data() + monk * n, n);
        articulator_->mix(monk_l_.data(), monk_r_.data(), static_cast<int>(n), left, right);
        left += n;
        right += n;
        frames -= n;
        tick_phase_ += n;
        if (tick_phase_ >= kTickFrames) {
            tick_phase_ = 0;
            articulator_->tick();
        }
    }
}

void Choir::render_plain(float *left, float *right, uint32_t frames) {
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

bool Choir::window_visible() const {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    return window_visible_;
}

bool Choir::monk_singing(int monk) const {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    if (monk < 0 || monk >= monks())
        return false;
    if (profile_mode_)
        return articulator_->singing(monk);
    return dl_is_active(handles_[static_cast<size_t>(monk)]) != 0;
}

float Choir::monk_vowel(int monk) const {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    if (monk < 0 || monk >= monks())
        return 0.5f;
    if (profile_mode_)
        return articulator_->vowel(monk);
    return dl_get_vowel(handles_[static_cast<size_t>(monk)]);
}

bool Choir::monk_lips_closed(int monk) const {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    return profile_mode_ && articulator_->lips_closed(monk);
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
    if (profile_mode_) {
        switch (number) {
        case 64:
            articulator_->sustain(value >= 64);
            break;
        case 120:
        case 123:
            articulator_->all_notes_off();
            break;
        case 121: // reset all controllers (GM RP-015: not volume)
            cc_[1] = 0;
            cc_[11] = 127;
            cc_[64] = 0;
            articulator_->sustain(false);
            bend_ = 8192;
            break;
        default:
            break;
        }
        update_channel();
        return;
    }
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
    if (profile_mode_) {
        articulator_->all_notes_off();
        return;
    }
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
            released_[static_cast<size_t>(action.monk)] = false;
            set_level(action.monk, level_for(action.velocity));
            dl_note_on(handle, note, static_cast<float>(action.velocity) / 127.0f);
        } else {
            dl_note_off(handle, note);
            // A stolen monk already sings its new note.
            if (allocator_.note_of(action.monk) < 0)
                released_[static_cast<size_t>(action.monk)] = true;
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

void Choir::set_level(int monk, float level) {
    levels_[static_cast<size_t>(monk)] = level;
    dl_set_level(handles_[static_cast<size_t>(monk)], level);
}

// Monks without a note keep their last velocity, so release tails follow a
// falling volume, but never a rising one: after a note-off the DSP still
// plays the last grains (up to 20 ms, or the envelope's release), and a gain
// or CC11 reset before the next note would replay them at full level, a
// blip of the old note before the new one. The next note-on sets the level.
void Choir::update_levels() {
    for (int monk = 0; monk < monks(); ++monk) {
        const size_t index = static_cast<size_t>(monk);
        float level = level_for(allocator_.velocity_of(monk));
        if (released_[index])
            level = std::min(level, levels_[index]);
        set_level(monk, level);
    }
}

// Profile mode's level headroom: the DSP volume is raised by this factor and
// the articulator's levels lowered by it, so the product (the sound) is the
// same, but levels above the DSP's cap of 1 work: onset.peak over 1 and the
// onset.bandwidth make-up gain (D33). Capped so the volume stays <= 1.
double Choir::level_headroom() const {
    return volume_ > 0.25f ? 1.0 / static_cast<double>(volume_) : 4.0;
}

void Choir::update_channel() {
    const double offset = bend_ - 8192;
    const double semitones = offset / (offset >= 0 ? 8191.0 : 8192.0) * bend_range_;
    articulator_->set_channel(semitones, cc_[7], cc_[11], cc_[1], gain_ / level_headroom());
}

void Choir::set_profile_mode(bool on) {
    if (on == profile_mode_)
        return;
    if (on) {
        // Plain notes stop; the articulator takes over the synths.
        allocator_.all_notes_off(actions_);
        actions_.clear();
        for (DelayLamaHandle *handle : handles_)
            dl_all_notes_off(handle);
        profile_mode_ = true;
        tick_phase_ = 0;
        for (DelayLamaHandle *handle : handles_)
            dl_set_volume(handle, static_cast<float>(volume_ * level_headroom()));
        update_channel();
        articulator_->start();
        return;
    }
    articulator_->stop();
    profile_mode_ = false;
    for (DelayLamaHandle *handle : handles_) {
        dl_set_volume(handle, volume_);
        dl_set_glide(handle, glide_);
        dl_set_voice(handle, voice_);
        dl_set_vowel(handle, vowel_);
        dl_set_vibrato(handle, vibrato_);
        dl_set_vibrato_rate(handle, vibrato_rate_);
        dl_set_breath(handle, breath_);
        dl_set_delay(handle, echo_amount_);
        dl_set_vibrato_seed(handle, 12345u);
    }
    apply_bend();
    apply_envelope();
    apply_unison();
    for (int monk = 0; monk < monks(); ++monk)
        set_level(monk, level_for(100));
}

void Choir::apply_bend() {
    if (profile_mode_) {
        update_channel();
        return;
    }
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
    if (key == "articulator") {
        if (value != 0.0 && value != 1.0) {
            error = "set articulator takes 0 or 1";
            return false;
        }
        set_profile_mode(value == 1.0);
        return true;
    }
    if (key == "window.visible") {
        // Both modes; the window reads it on its next animation tick.
        if (value != 0.0 && value != 1.0) {
            error = "set window.visible takes 0 or 1";
            return false;
        }
        window_visible_ = value == 1.0;
        return true;
    }
    if (Articulator::handles_key(key))
        return articulator_->set(key, value, error);
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
        if (profile_mode_)
            update_channel();
        else
            update_levels();
    } else if (key == "volume") {
        volume_ = v;
        if (profile_mode_) {
            each(dl_set_volume, static_cast<float>(volume_ * level_headroom()));
            update_channel();
        } else {
            each(dl_set_volume, v);
        }
    } else if (profile_mode_) {
        // Protocol 0's sound keys belong to plain mode; in profile mode the
        // profile owns them (IMITATION.md 13).
        error = "set " + key + " is a plain-mode key; use the profile keys";
        return false;
    } else if (key == "glide") {
        glide_ = v;
        each(dl_set_glide, v);
    } else if (key == "voice") {
        voice_ = v;
        each(dl_set_voice, v);
    } else if (key == "vowel") {
        vowel_ = v;
        each(dl_set_vowel, v);
    } else if (key == "vibrato") {
        vibrato_ = v;
        each(dl_set_vibrato, v);
    } else if (key == "vibrato_rate") {
        vibrato_rate_ = v;
        each(dl_set_vibrato_rate, v);
    } else if (key == "breath") {
        breath_ = v;
        each(dl_set_breath, v);
    } else if (key == "echo_amount") {
        echo_amount_ = v;
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
