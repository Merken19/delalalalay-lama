#include "voice_allocator.h"

#include <algorithm>

namespace delay_lama::voice {

VoiceAllocator::VoiceAllocator(int monks)
    : monks_(static_cast<size_t>(std::max(1, std::min(kMaxMonks, monks)))) {}

int VoiceAllocator::note_of(int monk) const {
    if (monk < 0 || monk >= this->monks())
        return -1;
    return monks_[static_cast<size_t>(monk)].note;
}

int VoiceAllocator::velocity_of(int monk) const {
    if (monk < 0 || monk >= this->monks())
        return 0;
    return monks_[static_cast<size_t>(monk)].velocity;
}

int VoiceAllocator::monk_of(int note) const {
    for (int monk = 0; monk < monks(); ++monk) {
        if (monks_[static_cast<size_t>(monk)].note == note)
            return monk;
    }
    return -1;
}

int VoiceAllocator::pick_free(const SoundingFn &sounding) const {
    int tail = -1;
    for (int monk = 0; monk < monks(); ++monk) {
        const Monk &state = monks_[static_cast<size_t>(monk)];
        if (state.note >= 0)
            continue;
        if (!sounding || !sounding(monk))
            return monk;
        if (tail < 0 || state.freed < monks_[static_cast<size_t>(tail)].freed)
            tail = monk;
    }
    return tail;
}

// Called only when every monk has a note.
int VoiceAllocator::pick_victim() const {
    int highest = -1;
    int lowest = 128;
    for (const Monk &state : monks_) {
        highest = std::max(highest, state.note);
        lowest = std::min(lowest, state.note);
    }
    const auto better = [&](int candidate, int best) {
        if (best < 0)
            return true;
        const Monk &a = monks_[static_cast<size_t>(candidate)];
        const Monk &b = monks_[static_cast<size_t>(best)];
        if (a.sustained != b.sustained)
            return a.sustained;
        return a.started < b.started;
    };
    int victim = -1;
    for (int monk = 0; monk < monks(); ++monk) {
        const int note = monks_[static_cast<size_t>(monk)].note;
        if (note != highest && note != lowest && better(monk, victim))
            victim = monk;
    }
    if (victim >= 0)
        return victim;
    for (int monk = 0; monk < monks(); ++monk) {
        if (better(monk, victim))
            victim = monk;
    }
    return victim;
}

void VoiceAllocator::assign(int monk, int note, int velocity) {
    Monk &state = monks_[static_cast<size_t>(monk)];
    state.note = note;
    state.velocity = velocity;
    state.sustained = false;
    state.started = ++clock_;
}

void VoiceAllocator::release(int monk, std::vector<MonkAction> &actions) {
    Monk &state = monks_[static_cast<size_t>(monk)];
    if (state.note < 0)
        return;
    actions.push_back({MonkAction::Kind::Off, monk, state.note, 0});
    state.note = -1;
    state.sustained = false;
    state.freed = ++clock_;
}

void VoiceAllocator::note_on(int note, int velocity, const SoundingFn &sounding,
                             std::vector<MonkAction> &actions) {
    int monk = monk_of(note);
    if (monk < 0)
        monk = pick_free(sounding);
    if (monk >= 0) {
        actions.push_back({MonkAction::Kind::On, monk, note, velocity});
        assign(monk, note, velocity);
        return;
    }
    monk = pick_victim();
    const int stolen = monks_[static_cast<size_t>(monk)].note;
    actions.push_back({MonkAction::Kind::On, monk, note, velocity});
    actions.push_back({MonkAction::Kind::Off, monk, stolen, 0});
    assign(monk, note, velocity);
}

void VoiceAllocator::note_off(int note, std::vector<MonkAction> &actions) {
    const int monk = monk_of(note);
    if (monk < 0)
        return;
    if (sustain_)
        monks_[static_cast<size_t>(monk)].sustained = true;
    else
        release(monk, actions);
}

void VoiceAllocator::set_sustain(bool down, std::vector<MonkAction> &actions) {
    sustain_ = down;
    if (down)
        return;
    for (int monk = 0; monk < monks(); ++monk) {
        if (monks_[static_cast<size_t>(monk)].sustained)
            release(monk, actions);
    }
}

void VoiceAllocator::all_notes_off(std::vector<MonkAction> &actions) {
    for (int monk = 0; monk < monks(); ++monk)
        release(monk, actions);
}

} // namespace delay_lama::voice
