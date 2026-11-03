// Checks for the DelayLamaVoice choir logic: the voice allocator
// (Windows/voice_allocator.h) and the per-monk idle animation
// (Windows/monk_animation.h). No assert(): CTest runs Release builds.

#include "monk_animation.h"
#include "voice_allocator.h"

#include <cstdio>
#include <set>
#include <string>
#include <vector>

using namespace delay_lama;
using namespace delay_lama::voice;

namespace {

int failures = 0;

void check(bool condition, const std::string &what) {
    if (!condition) {
        std::fprintf(stderr, "voice_allocator: FAIL %s\n", what.c_str());
        ++failures;
    }
}

bool is(const MonkAction &action, MonkAction::Kind kind, int monk, int note) {
    return action.kind == kind && action.monk == monk && action.note == note;
}

const VoiceAllocator::SoundingFn kSilent = [](int) { return false; };

void basic_choir() {
    VoiceAllocator choir(4);
    std::vector<MonkAction> actions;
    choir.note_on(60, 100, kSilent, actions);
    choir.note_on(64, 90, kSilent, actions);
    choir.note_on(67, 80, kSilent, actions);
    check(actions.size() == 3 && is(actions[0], MonkAction::Kind::On, 0, 60) &&
              is(actions[1], MonkAction::Kind::On, 1, 64) &&
              is(actions[2], MonkAction::Kind::On, 2, 67),
          "a chord goes to monks 0, 1, 2");
    check(actions.size() == 3 && actions[1].velocity == 90, "velocity is passed on");
    check(choir.velocity_of(1) == 90 && choir.monk_of(67) == 2 && choir.note_of(3) == -1,
          "the allocator reports who sings what");

    actions.clear();
    choir.note_off(64, actions);
    check(actions.size() == 1 && is(actions[0], MonkAction::Kind::Off, 1, 64),
          "note-off releases its monk");
    actions.clear();
    choir.note_on(72, 100, kSilent, actions);
    check(actions.size() == 1 && is(actions[0], MonkAction::Kind::On, 1, 72),
          "the lowest-numbered silent monk takes the next note");

    actions.clear();
    choir.note_on(60, 50, kSilent, actions);
    check(actions.size() == 1 && is(actions[0], MonkAction::Kind::On, 0, 60) &&
              choir.velocity_of(0) == 50,
          "a repeated note is sung again by the same monk");

    actions.clear();
    choir.note_off(61, actions);
    check(actions.empty(), "a note-off for a note nobody sings does nothing");

    actions.clear();
    choir.all_notes_off(actions);
    check(actions.size() == 3, "all notes off releases every singing monk");
    for (int monk = 0; monk < 4; ++monk)
        check(choir.note_of(monk) == -1, "monk " + std::to_string(monk) + " is free");
}

void stealing_spares_melody_and_bass() {
    VoiceAllocator choir(4);
    std::vector<MonkAction> actions;
    for (const int note : {60, 64, 67, 72})
        choir.note_on(note, 100, kSilent, actions);
    actions.clear();
    choir.note_on(76, 100, kSilent, actions);
    // Lowest 60 and highest 72 survive; 64 is the oldest of the rest.
    check(actions.size() == 2 && is(actions[0], MonkAction::Kind::On, 1, 76) &&
              is(actions[1], MonkAction::Kind::Off, 1, 64),
          "a fifth note steals the oldest middle note, gliding (on before off)");
    check(choir.monk_of(60) == 0 && choir.monk_of(72) == 3 && choir.monk_of(64) == -1,
          "melody and bass keep their monks");

    // Now 60 67 72 76; the middle notes are 67 (older) and 72.
    actions.clear();
    choir.note_on(55, 100, kSilent, actions);
    check(actions.size() == 2 && is(actions[0], MonkAction::Kind::On, 2, 55) &&
              is(actions[1], MonkAction::Kind::Off, 2, 67),
          "a new bass note steals the oldest middle note too");
}

void stealing_prefers_pedal_notes() {
    VoiceAllocator choir(4);
    std::vector<MonkAction> actions;
    choir.set_sustain(true, actions);
    for (const int note : {60, 62, 64, 67})
        choir.note_on(note, 100, kSilent, actions);
    choir.note_off(64, actions);
    check(actions.size() == 4 && choir.monk_of(64) == 2,
          "with the pedal down a released note keeps its monk");
    actions.clear();
    choir.note_on(65, 100, kSilent, actions);
    check(actions.size() == 2 && is(actions[1], MonkAction::Kind::Off, 2, 64),
          "a note kept only by the pedal is stolen before an older held one");
}

void small_choirs() {
    VoiceAllocator solo(1);
    std::vector<MonkAction> actions;
    solo.note_on(60, 100, kSilent, actions);
    actions.clear();
    solo.note_on(62, 100, kSilent, actions);
    check(actions.size() == 2 && is(actions[0], MonkAction::Kind::On, 0, 62) &&
              is(actions[1], MonkAction::Kind::Off, 0, 60),
          "one monk: last note wins and glides");
    actions.clear();
    solo.note_off(60, actions);
    check(actions.empty(), "one monk: releasing the stolen note does nothing");
    solo.note_off(62, actions);
    check(actions.size() == 1 && is(actions[0], MonkAction::Kind::Off, 0, 62),
          "one monk: releasing the sung note stops it");

    VoiceAllocator duo(2);
    actions.clear();
    duo.note_on(60, 100, kSilent, actions);
    duo.note_on(64, 100, kSilent, actions);
    actions.clear();
    duo.note_on(67, 100, kSilent, actions);
    check(actions.size() == 2 && is(actions[1], MonkAction::Kind::Off, 0, 60),
          "two monks have no middle note: the oldest is stolen");

    check(VoiceAllocator(0).monks() == 1 && VoiceAllocator(20).monks() == 8,
          "the choir size is clamped to 1..8");
}

void sustain_pedal() {
    VoiceAllocator choir(4);
    std::vector<MonkAction> actions;
    choir.set_sustain(true, actions);
    choir.note_on(60, 100, kSilent, actions);
    choir.note_off(60, actions);
    check(actions.size() == 1 && choir.monk_of(60) == 0, "the pedal holds a released note");
    actions.clear();
    choir.note_on(60, 70, kSilent, actions);
    check(actions.size() == 1 && is(actions[0], MonkAction::Kind::On, 0, 60),
          "replaying a pedal-held note uses its monk");
    choir.note_on(64, 100, kSilent, actions);
    choir.note_off(60, actions);
    actions.clear();
    choir.set_sustain(false, actions);
    check(actions.size() == 1 && is(actions[0], MonkAction::Kind::Off, 0, 60) &&
              choir.monk_of(64) == 1,
          "pedal up releases only the notes whose keys are up");
    actions.clear();
    choir.set_sustain(true, actions);
    choir.note_off(64, actions);
    choir.all_notes_off(actions);
    check(actions.size() == 1 && is(actions[0], MonkAction::Kind::Off, 1, 64),
          "all notes off also releases pedal-held notes");
}

void release_tails() {
    VoiceAllocator choir(3);
    std::vector<MonkAction> actions;
    std::set<int> sounding;
    const VoiceAllocator::SoundingFn tails = [&](int monk) { return sounding.count(monk) > 0; };
    choir.note_on(60, 100, tails, actions);
    choir.note_on(64, 100, tails, actions);
    choir.note_off(60, actions);
    choir.note_off(64, actions);
    sounding = {0, 1};
    actions.clear();
    choir.note_on(67, 100, tails, actions);
    check(actions.size() == 1 && actions[0].monk == 2,
          "a silent monk is preferred over ones still in their release");
    choir.note_off(67, actions);
    sounding = {0, 1, 2};
    actions.clear();
    choir.note_on(69, 100, tails, actions);
    check(actions.size() == 1 && actions[0].monk == 0,
          "with every free monk still sounding, the longest-released one is used");
}

void idle_animation() {
    MonkAnimator monk;
    monk.restart_idle(1000);
    check(monk.frame(false, 0.5f, 1000) == 5, "idle starts on the hold frame");
    check(monk.frame(false, 0.5f, 1000 + 1450) == 2, "the first blink is at 1.4 s");
    check(monk.frame(false, 0.5f, 1000 + 1800) == 5, "the blink ends at 1.7 s");
    check(monk.frame(false, 0.5f, 1000 + 4800) == 5 && monk.frame(false, 0.5f, 1000 + 5000) == 3,
          "the shuffle starts at 4.8 s");
    check(monk.frame(true, 0.5f, 7000) == 18 && monk.frame(true, 0.0f, 7000) == 6 &&
              monk.frame(true, 1.0f, 7000) == 29,
          "singing maps the vowel onto frames 6..29");
    check(monk.frame(false, 0.5f, 9000) == 5 && monk.frame(false, 0.5f, 9000 + 1450) == 2,
          "the idle sequence restarts when singing stops");

    MonkAnimator offset(MonkAnimator::choir_idle_offset(2)); // 600 ms
    offset.restart_idle(0);
    check(offset.frame(false, 0.5f, 800) == 2, "an idle offset starts the sequence that far in");
    check(MonkAnimator::choir_idle_offset(0) == 0, "the first choir monk idles like the standalone");

    // After a chord ends, no two of the eight monks blink at the same moment.
    std::vector<MonkAnimator> choir;
    for (int index = 0; index < 8; ++index) {
        choir.emplace_back(MonkAnimator::choir_idle_offset(index));
        choir.back().restart_idle(0);
    }
    std::set<uint64_t> offsets;
    for (int index = 0; index < 8; ++index)
        offsets.insert(MonkAnimator::choir_idle_offset(index));
    check(offsets.size() == 8, "every choir monk has its own idle offset");
    // The shuffle shows the blink frame too; 20 s covers several of its loops.
    bool unison = false;
    for (uint64_t now = 0; now < 20000; now += 100) {
        int blinking = 0;
        for (MonkAnimator &animator : choir)
            blinking += animator.frame(false, 0.5f, now) == MonkAnimator::kIdleBlinkFrame;
        unison = unison || blinking > 1;
    }
    check(!unison, "choir monks never blink in unison");
}

} // namespace

int main() {
    basic_choir();
    stealing_spares_melody_and_bass();
    stealing_prefers_pedal_notes();
    small_choirs();
    sustain_pedal();
    release_tails();
    idle_animation();
    if (failures != 0) {
        std::fprintf(stderr, "voice_allocator: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("voice_allocator: PASS\n");
    return 0;
}
