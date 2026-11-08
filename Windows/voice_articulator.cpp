#include "voice_articulator.h"

extern "C" {
#include "../DSP/delay.h"
}

#include <algorithm>
#include <cmath>
#include <cstdio>

struct MonkDelayHolder {
    MonkDelay delay;
};

namespace delay_lama::voice {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kChordMs = 30.0;  // notes starting this close together are a chord (8.1)
constexpr double kQuickEndMs = 10; // allnotesoff fade (8.8)
constexpr double kLegatoLevelMs = 40.0;
constexpr double kSampleRateHz = 48000.0;

constexpr KeyInfo kKeyInfo[] = {
#define DL_KEY(id, name, low, high, def, nullable) {name, low, high, def, nullable},
#include "voice_profile_keys.inc"
#undef DL_KEY
};

struct ChannelKey {
    const char *name;
    double low;
    double high;
};

constexpr ChannelKey kChannelKeys[] = {
    {"profile.begin", 0, 1},    {"profile.apply", 0, 1},      {"profile.reset", 0, 1},
    {"exaggerate", 0, 2},       {"range.shift", -48, 48},     {"choir.voice_var", 0, 0.1},
    {"choir.vibrato_var", 0, 0.2}, {"choir.seed", 0, 1},      {"choir.pan", 0, 1},
    {"space.echo", 0, 1},       {"space.echo_time", 0, 1},    {"space.echo_feedback", 0, 0.95},
    {"mouth.closed_below", 0, 1},
};

double clamp(double x, double low, double high) { return x < low ? low : (x > high ? high : x); }
double clamp01(double x) { return clamp(x, 0.0, 1.0); }

// The curve shapes of IMITATION.md section 5, x in 0..1.
double shape_of(int curve, double x) {
    x = clamp01(x);
    switch (curve) {
    case 0: // step
        return x < 1.0 ? 0.0 : 1.0;
    case 1: // linear
        return x;
    case 2: // exp: 95% of the way at x = 1
        return (1.0 - std::exp(-3.0 * x)) / (1.0 - std::exp(-3.0));
    case 4: // late
        return x * x;
    case 3: // ease
    default:
        return x * x * (3.0 - 2.0 * x);
    }
}

// From a to b over `time` ms (0: at once), `t` ms after the move started.
double move(double a, double b, double time, double t, int curve) {
    if (time <= 0.0)
        return b;
    return a + (b - a) * shape_of(curve, t / time);
}

// DSP vibrato rate 0..1 for a rate in Hz: 6 * 4^(2r - 1) Hz (section 2).
double dsp_vibrato_rate(double hz) {
    return clamp01((std::log(std::max(hz, 0.1) / 6.0) / std::log(4.0) + 1.0) / 2.0);
}

} // namespace

const KeyInfo &key_info(Key key) { return kKeyInfo[static_cast<int>(key)]; }

bool find_key(const std::string &name, Key &key) {
    for (int index = 0; index < kKeyCount; ++index) {
        if (name == kKeyInfo[index].name) {
            key = static_cast<Key>(index);
            return true;
        }
    }
    return false;
}

Profile::Profile() {
    for (int index = 0; index < kKeyCount; ++index)
        v[static_cast<size_t>(index)] = kKeyInfo[index].def;
}

Articulator::Articulator(std::vector<DelayLamaHandle *> handles)
    : handles_(std::move(handles)), monks_(handles_.size()),
      delay_(std::make_unique<MonkDelayHolder>()) {
    monk_delay_init(&delay_->delay, static_cast<float>(kSampleRateHz));
    monk_delay_set_mix(&delay_->delay, 0.0f);
    monk_delay_set_rate(&delay_->delay, static_cast<float>(echo_time_));
    monk_delay_set_feedback(&delay_->delay, static_cast<float>(echo_feedback_));
}

Articulator::~Articulator() = default;

bool Articulator::handles_key(const std::string &key) {
    Key unused;
    if (find_key(key, unused))
        return true;
    for (const ChannelKey &channel : kChannelKeys) {
        if (key == channel.name)
            return true;
    }
    return false;
}

void Articulator::start() {
    for (DelayLamaHandle *handle : handles_) {
        dl_voice_note_off(handle);
        dl_set_envelope(handle, 0.0f, 0.0f, 1.0f, 0.0f);
        dl_set_delay(handle, 0.0f);
        dl_set_glide(handle, 0.0f);
        dl_set_level(handle, 0.0f);
        dl_set_formant_bandwidth(handle, 1.0f);
        for (int f = 0; f < 3; ++f)
            dl_set_formant_scale(handle, f, 1.0f);
    }
    for (Monk &monk : monks_)
        monk = Monk();
    quiet_since_ = -1e9;
    phrase_start_ = -1e9;
    phrase_rest_ = 0.0;
    monk_delay_reset(&delay_->delay);
    apply_choir();
    apply_unison();
}

void Articulator::stop() {
    for (DelayLamaHandle *handle : handles_) {
        dl_voice_note_off(handle);
        dl_set_formant_bandwidth(handle, 1.0f); // plain mode keeps phase 1's sound
        for (int f = 0; f < 3; ++f)
            dl_set_formant_scale(handle, f, 1.0f);
    }
    for (Monk &monk : monks_)
        monk = Monk();
}

void Articulator::set_channel(double bend_semitones, int volume, int expression, int modwheel,
                              double gain) {
    bend_ = bend_semitones;
    volume_ = volume;
    expression_ = expression;
    modwheel_ = modwheel;
    gain_ = gain;
}

void Articulator::apply_choir() {
    const int count = static_cast<int>(monks_.size());
    for (int index = 0; index < count; ++index) {
        Monk &monk = monks_[static_cast<size_t>(index)];
        // Voice size alternates around the profile's: 0, +var, -var, +var...
        monk.voice_offset = index == 0 ? 0.0 : (index % 2 ? voice_var_ : -voice_var_);
        // Vibrato rate spread evenly across the monks.
        monk.rate_factor =
            count > 1 ? 1.0 + vibrato_var_ * (2.0 * index / (count - 1) - 1.0) : 1.0;
        // Own jitter seeds (D17, D18); 12345 is the DSP's own start value.
        dl_set_vibrato_seed(handles_[static_cast<size_t>(index)],
                            seed_ ? 12345u + 104729u * static_cast<uint32_t>(index + 1)
                                  : 12345u);
    }
}

void Articulator::apply_unison() {
    for (DelayLamaHandle *handle : handles_)
        dl_set_unison(handle, static_cast<int32_t>(profile_[Key::EnsembleUnison]),
                      static_cast<float>(profile_[Key::EnsembleDetune]),
                      static_cast<float>(profile_[Key::EnsembleSpread]));
}

bool Articulator::set(const std::string &key, double value, std::string &error) {
    char text[160];
    Key profile_key;
    if (find_key(key, profile_key)) {
        const KeyInfo &info = key_info(profile_key);
        if (info.nullable && value < 0.0) {
            value = -1.0;
        } else if (value < info.low || value > info.high) {
            snprintf(text, sizeof(text), "set %s takes %g to %g%s", info.name, info.low,
                     info.high, info.nullable ? " or -1" : "");
            error = text;
            return false;
        }
        target_profile()[profile_key] = value;
        if (!staging_ && (profile_key == Key::EnsembleUnison || profile_key == Key::EnsembleDetune ||
                          profile_key == Key::EnsembleSpread))
            apply_unison();
        return true;
    }
    const ChannelKey *channel = nullptr;
    for (const ChannelKey &candidate : kChannelKeys) {
        if (key == candidate.name)
            channel = &candidate;
    }
    if (!channel) {
        error = "unknown set key " + key;
        return false;
    }
    if (value < channel->low || value > channel->high) {
        snprintf(text, sizeof(text), "set %s takes %g to %g", channel->name, channel->low,
                 channel->high);
        error = text;
        return false;
    }
    if (key == "profile.begin") {
        if (value != 0.0) {
            staging_ = true;
            staged_ = profile_;
        }
    } else if (key == "profile.apply") {
        if (value != 0.0 && staging_) {
            profile_ = staged_;
            staging_ = false;
            apply_unison();
        }
    } else if (key == "profile.reset") {
        if (value != 0.0) {
            target_profile() = Profile();
            if (!staging_)
                apply_unison();
        }
    } else if (key == "exaggerate") {
        exaggerate_ = value;
    } else if (key == "range.shift") {
        shift_ = value;
    } else if (key == "choir.voice_var") {
        voice_var_ = value;
        apply_choir();
    } else if (key == "choir.vibrato_var") {
        vibrato_var_ = value;
        apply_choir();
    } else if (key == "choir.seed") {
        seed_ = value >= 0.5;
        apply_choir();
    } else if (key == "choir.pan") {
        pan_ = value;
    } else if (key == "space.echo") {
        echo_ = value;
        monk_delay_set_mix(&delay_->delay, static_cast<float>(value));
    } else if (key == "space.echo_time") {
        echo_time_ = value;
        monk_delay_set_rate(&delay_->delay, static_cast<float>(value));
    } else if (key == "space.echo_feedback") {
        echo_feedback_ = value;
        monk_delay_set_feedback(&delay_->delay, static_cast<float>(value));
    } else {
        closed_below_ = value;
    }
    return true;
}

bool Articulator::next(const std::string &key, double value, std::string &error) {
    Key profile_key;
    if (!find_key(key, profile_key)) {
        error = "unknown next key " + key + " (next takes profile keys)";
        return false;
    }
    const KeyInfo &info = key_info(profile_key);
    if (info.nullable && value < 0.0) {
        value = -1.0;
    } else if (value < info.low || value > info.high) {
        char text[160];
        snprintf(text, sizeof(text), "next %s takes %g to %g", info.name, info.low, info.high);
        error = text;
        return false;
    }
    next_.emplace_back(profile_key, value);
    return true;
}

double Articulator::base_level(const Note &n) const {
    // Velocity on the profile's curve (D21: (v/127)^1 by default), CC7 and
    // CC11 on the GM curve (D8), times the channel gain and the profile's
    // dyn.gain.
    const double velocity = std::pow(n.velocity / 127.0, n.p[Key::DynVelocity]);
    const double volume = volume_ / 127.0;
    const double expression = expression_ / 127.0;
    return gain_ * n.p[Key::DynGain] * velocity * volume * volume * expression * expression;
}

double Articulator::ioi(int monk) const {
    const Monk &m = monks_[static_cast<size_t>(monk)];
    const double since = now_ - m.start;
    return m.n.note >= 0 && since > 0.0 && since < 2000.0 ? since : -1.0;
}

int Articulator::pick_monk(int note, Join &join) const {
    const int count = static_cast<int>(monks_.size());
    double mode = profile_[Key::PhraseMode];
    double overlap_ms = profile_[Key::PhraseOverlap];
    double gap = profile_[Key::PhraseGap];
    for (const auto &[key, value] : next_) {
        if (key == Key::PhraseMode)
            mode = value;
        else if (key == Key::PhraseOverlap)
            overlap_ms = value;
        else if (key == Key::PhraseGap)
            gap = value;
    }

    // A repeated pitch is always re-articulated on its monk (8.2).
    for (int index = 0; index < count; ++index) {
        const Monk &m = monks_[static_cast<size_t>(index)];
        if ((m.key_down || m.sustained) && m.n.note == note && m.state != State::Idle) {
            join = Join::Rearticulate;
            return index;
        }
    }

    // One note held on a line, started earlier than a chord would: legato
    // (8.2 auto with durations, 8.5 without).
    int held = -1;
    int held_count = 0;
    for (int index = 0; index < count; ++index) {
        const Monk &m = monks_[static_cast<size_t>(index)];
        if (m.key_down && (m.state == State::Sounding || m.state == State::Dip)) {
            held = index;
            ++held_count;
        }
    }
    if (held_count == 1 && (mode == 0.0 || mode == 1.0)) {
        const Monk &m = monks_[static_cast<size_t>(held)];
        if (now_ - m.start > kChordMs) {
            if (mode == 1.0 || m.n.duration < 0.0) {
                join = Join::Legato;
                return held;
            }
            const double overlap = m.start + m.n.duration - now_;
            join = overlap >= overlap_ms ? Join::Legato : Join::Rearticulate;
            return held;
        }
    }

    // A free monk; a connected one (its note ended within the gap) first.
    int connected = -1;
    for (int index = 0; index < count; ++index) {
        const Monk &m = monks_[static_cast<size_t>(index)];
        if (m.key_down || m.sustained || m.n.note < 0)
            continue;
        if (now_ - m.off_time <= gap &&
            (connected < 0 || m.off_time > monks_[static_cast<size_t>(connected)].off_time))
            connected = index;
    }
    if (connected >= 0) {
        const Monk &m = monks_[static_cast<size_t>(connected)];
        if (mode == 3.0 || m.state == State::Idle)
            join = Join::New;
        else
            join = mode == 1.0 ? Join::Legato : Join::Rearticulate;
        return connected;
    }
    for (const State wanted : {State::Idle, State::Ending}) {
        for (int index = 0; index < count; ++index) {
            const Monk &m = monks_[static_cast<size_t>(index)];
            if (!m.key_down && !m.sustained && m.state == wanted) {
                join = Join::New;
                return index;
            }
        }
    }

    // Every monk busy: steal (D11, O3). A note held only by the pedal first,
    // else the oldest note that is neither the highest nor the lowest.
    int victim = -1;
    for (int index = 0; index < count; ++index) {
        const Monk &m = monks_[static_cast<size_t>(index)];
        if (m.sustained && !m.key_down &&
            (victim < 0 || m.start < monks_[static_cast<size_t>(victim)].start))
            victim = index;
    }
    if (victim < 0) {
        int high = 0;
        int low = 0;
        for (int index = 1; index < count; ++index) {
            if (monks_[static_cast<size_t>(index)].n.note > monks_[static_cast<size_t>(high)].n.note)
                high = index;
            if (monks_[static_cast<size_t>(index)].n.note < monks_[static_cast<size_t>(low)].n.note)
                low = index;
        }
        for (int index = 0; index < count; ++index) {
            if (count >= 3 && (index == high || index == low))
                continue;
            if (victim < 0 ||
                monks_[static_cast<size_t>(index)].start < monks_[static_cast<size_t>(victim)].start)
                victim = index;
        }
    }
    // A stolen monk joins as its profile would join a connected note (8.6).
    join = mode == 1.0 ? Join::Legato : Join::Rearticulate;
    return victim;
}

double Articulator::rest_before_note() {
    bool held = false;
    for (const Monk &m : monks_)
        held = held || m.key_down || m.sustained;
    if (!held) {
        phrase_start_ = now_;
        phrase_rest_ = now_ - quiet_since_;
        return phrase_rest_;
    }
    return now_ - phrase_start_ <= kChordMs ? phrase_rest_ : 0.0;
}

void Articulator::note_released() {
    for (const Monk &m : monks_) {
        if (m.key_down || m.sustained)
            return;
    }
    quiet_since_ = now_;
}

void Articulator::send_bandwidth(int monk, double bandwidth) {
    Monk &m = monks_[static_cast<size_t>(monk)];
    if (std::fabs(bandwidth - 1.0) < 1e-6)
        bandwidth = 1.0; // exactly the DSP's own table again
    if (bandwidth == m.bandwidth ||
        (bandwidth != 1.0 && std::fabs(bandwidth - m.bandwidth) < 0.005))
        return;
    m.bandwidth = bandwidth;
    dl_set_formant_bandwidth(handles_[static_cast<size_t>(monk)], static_cast<float>(bandwidth));
}

// voice.formant1-3: sent only when they change (each change rebuilds a
// 1280-entry table in every unison voice).
void Articulator::send_formants(int monk, const double (&scale)[3]) {
    Monk &m = monks_[static_cast<size_t>(monk)];
    for (int f = 0; f < 3; ++f) {
        if (scale[f] == m.formant[f])
            continue;
        m.formant[f] = scale[f];
        dl_set_formant_scale(handles_[static_cast<size_t>(monk)], f, static_cast<float>(scale[f]));
    }
}

void Articulator::note_on(int note, int velocity, double duration_ms) {
    if (monks_.empty())
        return;
    if (velocity <= 0) {
        note_off(note);
        return;
    }
    Join join = Join::New;
    const int monk = pick_monk(note, join);
    begin_note(monk, note, velocity, duration_ms, join);
}

void Articulator::begin_note(int monk, int note, int velocity, double duration, Join join) {
    Monk &m = monks_[static_cast<size_t>(monk)];
    const bool sounding = m.state != State::Idle;
    if (!sounding && join != Join::New)
        join = Join::New;
    const double rest = rest_before_note();

    Note n;
    n.p = profile_;
    for (const auto &[key, value] : next_)
        n.p[key] = value;
    next_.clear();
    Profile &p = n.p;
    const double x = exaggerate_;
    n.note = note;
    n.velocity = velocity;
    n.duration = duration;

    // Pitch (6.1): the written pitch plus overrides; the DSP sings its note
    // an octave down, so it gets 12 more (D14).
    double sounding_note = note + 12.0 * p[Key::RangeOctave] + shift_;
    if (p[Key::RangeFold] >= 0.5) {
        for (int guard = 0; guard < 10 && sounding_note < p[Key::RangeLow]; ++guard)
            sounding_note += 12.0;
        for (int guard = 0; guard < 10 && sounding_note > p[Key::RangeHigh]; ++guard)
            sounding_note -= 12.0;
    }
    n.sounding = sounding_note;
    const double whole = std::floor(sounding_note + 0.5);
    n.fine = sounding_note - whole;
    n.dsp_note = static_cast<int>(clamp(whole + 12.0, 0.0, 126.0));

    // Nucleus (6.2) and the exaggerated moves (6.7).
    const double octaves = (sounding_note - 60.0) / 12.0;
    const double nucleus =
        clamp01(p[Key::NucleusVowel] + x * p[Key::NucleusPitchTrack] * octaves +
                x * p[Key::NucleusVelocityTrack] * (velocity - 64) / 63.0);
    n.nucleus = nucleus;
    const auto exaggerated = [&](double value) {
        return value < 0.0 ? -1.0 : clamp01(nucleus + x * (value - nucleus));
    };
    n.onset_vowel = p[Key::OnsetVowel] < 0.0 ? nucleus : exaggerated(p[Key::OnsetVowel]);
    // onset.after_rest (D33): the onset move only after the channel rested.
    if (p[Key::OnsetAfterRest] > 0.0 && rest < p[Key::OnsetAfterRest])
        n.onset_vowel = nucleus;
    n.bandwidth = std::fabs(n.onset_vowel - nucleus) > 0.02 ? p[Key::OnsetBandwidth] : 1.0;
    n.body_vowel = exaggerated(p[Key::BodyVowel]);
    p[Key::EndingVowel] = exaggerated(p[Key::EndingVowel]);
    p[Key::PhraseDipVowel] = exaggerated(p[Key::PhraseDipVowel]);
    p[Key::OnsetScoop] = clamp(x * p[Key::OnsetScoop], -12.0, 12.0);
    p[Key::EndingFall] = clamp(x * p[Key::EndingFall], -12.0, 12.0);
    p[Key::VibratoDepth] = clamp(0.2 + x * (p[Key::VibratoDepth] - 0.2), 0.2, 1.2);
    p[Key::PhraseDipLevel] = clamp01(1.0 - x * (1.0 - p[Key::PhraseDipLevel]));
    const double breath = p[Key::BodyBreath];
    if (p[Key::OnsetBurst] >= 0.0) {
        const double bv = p[Key::DynBurstVelocity];
        const double grow = (1.0 - bv) + bv * velocity / 127.0;
        n.burst = clamp01(breath + x * (p[Key::OnsetBurst] - breath) * grow);
    }
    n.peak = p[Key::OnsetPeak];

    // Fast notes (6.8).
    n.k = 1.0;
    if (p[Key::PhraseCompress] >= 0.5) {
        const double longest =
            std::max({p[Key::OnsetTime], p[Key::OnsetScoopTime], p[Key::OnsetAttack],
                      p[Key::OnsetBurstTime], p[Key::PhraseDipTime]});
        const double length = duration > 0.0 ? duration : ioi(monk);
        if (longest > 0.0 && length > 0.0)
            n.k = clamp(length / (3.0 * longest), 0.25, 1.0);
    }

    // vibrato.lock: a silent monk takes over the wobble of a locked monk that
    // sings (the one singing longest). Each synth's LFO only runs while it
    // sings, so without this the monks of a chord wobble apart even with one
    // seed. Monks that sing on stay in step, so a re-articulation or a legato
    // note keeps its LFO (no phase jump).
    if (!sounding && n.p[Key::VibratoLock] >= 0.5) {
        int leader = -1;
        for (int other = 0; other < static_cast<int>(monks_.size()); ++other) {
            const Monk &o = monks_[static_cast<size_t>(other)];
            if (other == monk || o.state == State::Idle || o.n.p[Key::VibratoLock] < 0.5)
                continue;
            if (leader < 0 || o.sound_start < monks_[static_cast<size_t>(leader)].sound_start)
                leader = other;
        }
        if (leader >= 0)
            dl_copy_vibrato(handles_[static_cast<size_t>(monk)],
                            handles_[static_cast<size_t>(leader)]);
    }

    // Where the previous note leaves off.
    if (sounding) {
        m.from_vowel = m.vowel;
        m.from_shape = m.shape;
        const double old_pitch = m.n.dsp_note + m.n.fine;
        m.glide_from = clamp(old_pitch - (n.dsp_note + n.fine), -12.0, 12.0);
        m.from_offset = m.offset;
        m.from_bandwidth = m.bandwidth;
    } else {
        m.from_offset = 0.0;
        m.from_bandwidth = 1.0;
        m.from_vowel = n.onset_vowel;
        m.from_shape = 0.0;
        m.glide_from = 0.0;
    }
    m.n = n;
    m.join = join;
    m.key_down = true;
    m.sustained = false;
    m.quick_end = false;
    m.start = now_;
    if (join == Join::Rearticulate && n.p[Key::PhraseDipTime] * n.k >= kTickMs) {
        m.state = State::Dip;
        update(monk);
    } else {
        start_sound(monk);
    }
}

void Articulator::start_sound(int monk) {
    Monk &m = monks_[static_cast<size_t>(monk)];
    DelayLamaHandle *handle = handles_[static_cast<size_t>(monk)];
    m.state = State::Sounding;
    m.sound_start = now_;
    // Legato glides on the DSP's constant-rate glide (D20); everything else
    // jumps (glide 0). Glide mode 1 draws the slide through the bend instead.
    const bool dsp_glide = m.join == Join::Legato && m.n.p[Key::PhraseGlideMode] < 0.5;
    dl_set_glide(handle, dsp_glide ? static_cast<float>(m.n.p[Key::PhraseGlide]) : 0.0f);
    // The note's first values must be in place before its note-on (the
    // phase 4a generator bugs, IMITATION.md 16).
    update(monk);
    dl_voice_note_on(handle, static_cast<uint8_t>(m.n.dsp_note));
}

void Articulator::start_ending(int monk, bool quick) {
    Monk &m = monks_[static_cast<size_t>(monk)];
    if (m.state == State::Idle)
        return;
    if (m.state == State::Ending && !quick)
        return;
    if (m.state == State::Dip)
        start_sound(monk);
    m.state = State::Ending;
    m.end_start = now_;
    m.end_level = m.shape;
    m.end_vowel = m.vowel;
    m.end_bandwidth = m.bandwidth;
    m.end_release = quick ? kQuickEndMs : m.n.p[Key::EndingRelease];
    m.quick_end = quick;
}

void Articulator::note_off(int note) {
    for (int index = 0; index < static_cast<int>(monks_.size()); ++index) {
        Monk &m = monks_[static_cast<size_t>(index)];
        if (!m.key_down || m.n.note != note)
            continue;
        m.key_down = false;
        m.off_time = now_;
        if (sustain_)
            m.sustained = true;
        else
            start_ending(index, false);
        note_released();
        return;
    }
}

void Articulator::sustain(bool down) {
    sustain_ = down;
    if (down)
        return;
    for (int index = 0; index < static_cast<int>(monks_.size()); ++index) {
        Monk &m = monks_[static_cast<size_t>(index)];
        if (m.sustained && !m.key_down) {
            m.sustained = false;
            start_ending(index, false);
        }
    }
    note_released();
}

void Articulator::all_notes_off() {
    for (int index = 0; index < static_cast<int>(monks_.size()); ++index) {
        Monk &m = monks_[static_cast<size_t>(index)];
        if (m.key_down)
            m.off_time = now_;
        m.key_down = false;
        m.sustained = false;
        start_ending(index, true);
    }
    note_released();
    next_.clear();
}

void Articulator::tick() {
    now_ += kTickMs;
    for (int monk = 0; monk < static_cast<int>(monks_.size()); ++monk)
        update(monk);
}

// One monk's values for this tick, sent to its synth (sections 6 and 10).
void Articulator::update(int monk) {
    Monk &m = monks_[static_cast<size_t>(monk)];
    DelayLamaHandle *handle = handles_[static_cast<size_t>(monk)];
    const Note &n = m.n;
    const Profile &p = n.p;
    const double k = n.k;

    if (m.state == State::Idle) {
        if (m.shape != 0.0) {
            m.shape = 0.0;
            dl_set_level(handle, 0.0f);
        }
        m.lips = false;
        send_bandwidth(monk, 1.0);
        return;
    }

    double vowel = n.nucleus;
    double shape = n.peak;
    double breath = p[Key::BodyBreath];
    double offset = n.fine; // semitones on top of the DSP note
    double vibrato_st = 0.2;
    double bandwidth = 1.0;
    bool lips = false;
    bool low_level_counts = false;
    const double octaves = (n.sounding - 60.0) / 12.0;

    if (m.state == State::Dip) {
        // Re-articulation (8.4): down to the dip level, the consonant's
        // colour, still on the old pitch; the new pitch starts at the bottom.
        const double t = now_ - m.start;
        const double dip_time = p[Key::PhraseDipTime] * k;
        if (t >= dip_time) {
            start_sound(monk);
            return;
        }
        const double fall = std::min(5.0, dip_time / 2.0);
        const double dip_vowel = p[Key::PhraseDipVowel] >= 0.0 ? p[Key::PhraseDipVowel]
                                                               : n.onset_vowel;
        shape = move(m.from_shape, p[Key::PhraseDipLevel], fall, t, 1);
        vowel = move(m.from_vowel, dip_vowel, fall, t, 3);
        bandwidth = move(m.from_bandwidth, n.bandwidth, fall, t, 1);
        offset = m.from_offset;
        lips = dip_vowel < 0.1;
        low_level_counts = true;
    } else if (m.state == State::Sounding) {
        const double u = now_ - m.sound_start;
        const double onset_time = p[Key::OnsetTime] * k;
        const bool vowel_moves = std::fabs(n.onset_vowel - n.nucleus) > 0.02 && onset_time > 0.0;
        double attack = p[Key::OnsetAttack] * k;
        // onset.mask: the onset colour is not heard at full level, since it
        // flares any harmonic it sits on (phase 4b diagnosis, IMITATION.md 16).
        if (m.join != Join::Legato && vowel_moves)
            attack = std::max(attack, p[Key::OnsetMask] * onset_time);

        // Vowel (6.3).
        double settle = onset_time;
        if (m.join == Join::Legato) {
            settle = p[Key::PhraseLegatoVowelTime];
            vowel = move(m.from_vowel, n.nucleus, settle, u, 3);
        } else {
            vowel = move(n.onset_vowel, n.nucleus, onset_time, u, static_cast<int>(p[Key::OnsetCurve]));
        }
        if (n.body_vowel >= 0.0 && u > settle)
            vowel = move(n.nucleus, n.body_vowel, p[Key::BodyVowelTime], u - settle, 2);

        // onset.bandwidth (D33): wide while the onset vowel moves, then back
        // to the DSP's own over the same time.
        if (m.join == Join::Legato)
            bandwidth = move(m.from_bandwidth, 1.0, kLegatoLevelMs, u, 1);
        else if (n.bandwidth != 1.0)
            bandwidth = u < onset_time ? n.bandwidth
                                       : move(n.bandwidth, 1.0, std::max(onset_time, 10.0),
                                              u - onset_time, 3);

        // Level (6.4): attack, then the decay toward the sustain.
        const double decay = p[Key::BodyDecay] > 0.0
                                 ? p[Key::BodyDecay] * std::pow(p[Key::BodyDecayTrack], octaves)
                                 : 0.0;
        const double floor = p[Key::BodySustain] * n.peak;
        const auto decayed = [&](double since) {
            return decay > 0.0 ? floor + (n.peak - floor) * std::exp(-3.0 * since / decay) : n.peak;
        };
        if (m.join == Join::Legato) {
            shape = move(m.from_shape, decayed(u), kLegatoLevelMs, u, 1);
        } else {
            const double from = m.join == Join::Rearticulate ? p[Key::PhraseDipLevel] : 0.0;
            shape = u < attack ? move(from, n.peak, attack, u, 1) : decayed(u - attack);
        }
        if (p[Key::BodyTremolo] > 0.0)
            shape *= 1.0 - p[Key::BodyTremolo] * 0.5 *
                               (1.0 - std::cos(2.0 * kPi * p[Key::BodyTremoloRate] * u / 1000.0));

        // Breath (6.5).
        if (n.burst >= 0.0 && m.join != Join::Legato)
            breath = move(n.burst, breath, p[Key::OnsetBurstTime] * k, u, 2);

        // Pitch offsets: the scoop, or a drawn glide (6.1, 8.3).
        if (m.join == Join::Legato) {
            if (p[Key::PhraseGlideMode] >= 0.5)
                offset += move(m.glide_from, 0.0, p[Key::PhraseGlideTime], u,
                               static_cast<int>(p[Key::PhraseGlideCurve]));
        } else if (p[Key::OnsetScoop] != 0.0) {
            offset += move(p[Key::OnsetScoop], 0.0, p[Key::OnsetScoopTime] * k, u,
                           static_cast<int>(p[Key::OnsetScoopCurve]));
        }

        // Vibrato (6.6): the floor, then a linear rise to the depth.
        const double delay = p[Key::VibratoDelay];
        if (u >= delay)
            vibrato_st = p[Key::VibratoFade] > 0.0
                             ? move(0.2, p[Key::VibratoDepth], p[Key::VibratoFade], u - delay, 1)
                             : p[Key::VibratoDepth];

        lips = p[Key::OnsetLips] >= 0.5 && u < std::max({onset_time, attack, 20.0});

        // Staccato cap and a decayed note's end (7.4).
        const bool capped = p[Key::BodyMax] > 0.0 && u >= p[Key::BodyMax];
        const bool died = decay > 0.0 && u > attack &&
                          shape < n.peak * std::pow(10.0, p[Key::BodyEndDb] / 20.0);
        if (capped || died) {
            m.shape = shape;
            m.vowel = vowel;
            start_ending(monk, false);
        }
    }

    if (m.state == State::Ending) {
        const double t = now_ - m.end_start;
        const double release = m.end_release;
        shape = m.end_level * std::exp(-3.0 * t / release);
        vowel = m.end_vowel;
        offset = n.fine;
        bandwidth = m.end_bandwidth;
        if (!m.quick_end) {
            if (p[Key::EndingVowel] >= 0.0) {
                const double time =
                    p[Key::EndingVowelTime] >= 0.0 ? p[Key::EndingVowelTime] : release;
                vowel = move(m.end_vowel, p[Key::EndingVowel], time, t, 3);
            }
            offset += move(0.0, p[Key::EndingFall], p[Key::EndingFallTime], t, 4);
            if (p[Key::EndingBreath] >= 0.0)
                breath = p[Key::EndingBreath];
        }
        lips = p[Key::EndingLips] >= 0.5 && t > 0.4 * release;
        low_level_counts = true;
        if (t >= 1.5 * release || shape < m.end_level * 0.01) {
            m.state = State::Idle;
            m.shape = 0.0;
            m.lips = false;
            dl_set_level(handle, 0.0f);
            dl_voice_note_off(handle);
            send_bandwidth(monk, 1.0);
            return;
        }
    }

    vibrato_st += p[Key::VibratoModwheel] * modwheel_ / 127.0;
    const double size = clamp01(p[Key::VoiceSize] + p[Key::VoicePitchTrack] * octaves +
                                m.voice_offset);
    // Wider formants ring shorter, so a grain carries less energy (about
    // 1/bandwidth, more on low notes, less on high ones); the level makes up
    // for it, so onset.bandwidth changes the colour of a vowel move, not its
    // loudness (onset.mask was the loudness). bandwidth^0.8 keeps a held
    // vowel within about +-3 dB at factors 2-4 from A1 to A5.
    const double level = clamp01(base_level(n) * shape * std::pow(bandwidth, 0.8));

    m.vowel = clamp01(vowel);
    m.shape = shape;
    m.breath = clamp01(breath);
    m.offset = offset;
    m.lips = lips || (low_level_counts && shape < closed_below_);

    dl_set_vowel(handle, static_cast<float>(m.vowel));
    dl_set_breath(handle, static_cast<float>(m.breath));
    dl_set_level(handle, static_cast<float>(level));
    dl_set_pitch_bend(handle, static_cast<float>(clamp(bend_ + offset, -12.0, 12.0)));
    // DSP vibrato depth: the peak deviation is depth + 0.2 st (section 2).
    dl_set_vibrato(handle, static_cast<float>(clamp01(vibrato_st - 0.2)));
    // Locked notes share one rate (vibrato.lock), so choir.vibrato_var is off.
    const double rate_factor = p[Key::VibratoLock] >= 0.5 ? 1.0 : m.rate_factor;
    dl_set_vibrato_rate(handle,
                        static_cast<float>(dsp_vibrato_rate(p[Key::VibratoRate] * rate_factor)));
    dl_set_voice(handle, static_cast<float>(size));
    send_bandwidth(monk, bandwidth);
    const double formants[3] = {p[Key::VoiceFormant1], p[Key::VoiceFormant2], p[Key::VoiceFormant3]};
    send_formants(monk, formants);
}

void Articulator::mix(const float *monk_l, const float *monk_r, int frames, float *left,
                      float *right) {
    const int count = static_cast<int>(monks_.size());
    scratch_.assign(static_cast<size_t>(frames) * 3, 0.0f);
    float *mono = scratch_.data();
    float *wet_l = mono + frames;
    float *wet_r = wet_l + frames;
    for (int monk = 0; monk < count; ++monk) {
        // Equal power, monk 1 left to monk K right (section 9); centre = 1.
        const double place = count > 1 ? static_cast<double>(monk) / (count - 1) : 0.5;
        const double angle = (0.5 + (place - 0.5) * pan_) * kPi / 2.0;
        const float gain_l = static_cast<float>(std::cos(angle) * std::sqrt(2.0));
        const float gain_r = static_cast<float>(std::sin(angle) * std::sqrt(2.0));
        const float *l = monk_l + static_cast<size_t>(monk) * frames;
        const float *r = monk_r + static_cast<size_t>(monk) * frames;
        for (int frame = 0; frame < frames; ++frame) {
            left[frame] += l[frame] * gain_l;
            right[frame] += r[frame] * gain_r;
            mono[frame] += 0.5f * (l[frame] + r[frame]);
        }
    }
    if (echo_ > 0.0) {
        monk_delay_process(&delay_->delay, mono, wet_l, wet_r, static_cast<uint32_t>(frames));
        for (int frame = 0; frame < frames; ++frame) {
            left[frame] += wet_l[frame] - mono[frame];
            right[frame] += wet_r[frame] - mono[frame];
        }
    }
}

bool Articulator::singing(int monk) const {
    return monk >= 0 && monk < static_cast<int>(monks_.size()) &&
           monks_[static_cast<size_t>(monk)].state != State::Idle;
}

float Articulator::vowel(int monk) const {
    if (monk < 0 || monk >= static_cast<int>(monks_.size()))
        return 0.5f;
    return static_cast<float>(monks_[static_cast<size_t>(monk)].vowel);
}

bool Articulator::lips_closed(int monk) const {
    return monk >= 0 && monk < static_cast<int>(monks_.size()) &&
           monks_[static_cast<size_t>(monk)].lips;
}

} // namespace delay_lama::voice
