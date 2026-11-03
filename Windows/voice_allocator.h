#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace delay_lama::voice {

// What the allocator asks one monk's synth to do.
struct MonkAction {
    enum class Kind { On, Off };
    Kind kind;
    int monk;
    int note;
    int velocity; // On only
};

// Gives each note of a channel its own monk (the choir), up to the choir
// size. Each monk sings one note at a time.
//
// - A note that a monk already sings (held or kept by the sustain pedal) is
//   sung again by that monk.
// - Otherwise a free monk takes it: the lowest-numbered silent one, or else
//   the one whose release tail started longest ago.
// - With every monk busy, one is stolen (Delay Lama Everything DECISIONS O3):
//   the oldest note, but never the current highest or lowest note, so melody
//   and bass survive. Notes kept only by the sustain pedal go first. A choir
//   of one or two has no middle note and steals the oldest. The stolen monk
//   glides to the new note (On before Off), like a mono synth playing legato.
// - While the sustain pedal is down, released notes keep their monk until the
//   pedal comes up.
class VoiceAllocator {
public:
    static constexpr int kMaxMonks = 8;
    // Whether a monk with no note still sounds (an envelope release tail).
    using SoundingFn = std::function<bool(int monk)>;

    explicit VoiceAllocator(int monks);

    int monks() const { return static_cast<int>(monks_.size()); }
    void note_on(int note, int velocity, const SoundingFn &sounding,
                 std::vector<MonkAction> &actions);
    void note_off(int note, std::vector<MonkAction> &actions);
    void set_sustain(bool down, std::vector<MonkAction> &actions);
    bool sustain() const { return sustain_; }
    // Releases every monk, pedal or not.
    void all_notes_off(std::vector<MonkAction> &actions);

    // The note a monk sings (held or sustained), or -1.
    int note_of(int monk) const;
    int velocity_of(int monk) const;
    // The monk singing a note, or -1.
    int monk_of(int note) const;

private:
    struct Monk {
        int note = -1;
        int velocity = 0;
        bool sustained = false; // key released, pedal keeps it
        uint64_t started = 0;
        uint64_t freed = 0;
    };

    int pick_free(const SoundingFn &sounding) const;
    int pick_victim() const;
    void assign(int monk, int note, int velocity);
    void release(int monk, std::vector<MonkAction> &actions);

    std::vector<Monk> monks_;
    uint64_t clock_ = 0;
    bool sustain_ = false;
};

} // namespace delay_lama::voice
