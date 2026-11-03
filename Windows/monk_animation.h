#pragma once

#include <cstddef>
#include <cstdint>

namespace delay_lama {

// Picks the monk's frame in faces.png. Singing maps the vowel across frames
// 6..29; otherwise the idle sequence from monksynth's MonkView runs from the
// moment the last note ended: hold frame 5 with two blinks on frame 2, then
// loop a 24-step mouth shuffle until the next note.
//
// Each monk keeps its own idle clock. A choir gives every monk a different
// idle offset, which starts its idle sequence that far in, so the monks don't
// blink or shuffle in unison. The standalone's single monk uses offset 0.
class MonkAnimator {
public:
    static constexpr uint64_t kIdleTickMs = 100;
    static constexpr uint64_t kIdleHoldTicks = 48;
    static constexpr int kIdleHoldFrame = 5;
    static constexpr int kIdleBlinkFrame = 2;
    static constexpr int kIdleShuffle[] = {5, 3, 4, 3, 2, 1, 0, 1, 5, 3, 4, 3,
                                           5, 1, 0, 1, 2, 3, 4, 3, 5, 1, 0, 1};
    static constexpr uint64_t kIdleShuffleTicksPerStep = 2;
    static constexpr int kFirstSingingFrame = 6;
    static constexpr int kLastSingingFrame = 29;

    // Idle offsets for the monks of a choir, in order. Found by a search so
    // that after a common note end no two monks ever show the blink frame at
    // once, in the hold phase or the shuffle (a regular step does not work:
    // 1.7 s is also the gap between the two blinks).
    static constexpr uint64_t kChoirIdleOffsetsMs[] = {0,    2700, 600,  3700,
                                                       4500, 3300, 3900, 3500};
    static uint64_t choir_idle_offset(int monk) {
        constexpr int count = sizeof(kChoirIdleOffsetsMs) / sizeof(kChoirIdleOffsetsMs[0]);
        return monk < 0 ? 0 : kChoirIdleOffsetsMs[monk % count];
    }

    explicit MonkAnimator(uint64_t idle_offset_ms = 0) : idle_offset_ms_(idle_offset_ms) {}

    void restart_idle(uint64_t now_ms) {
        was_singing_ = false;
        idle_start_ms_ = now_ms;
    }

    static int singing_frame(float vowel) {
        const int frame = kFirstSingingFrame + static_cast<int>(vowel * 23.0f + 0.5f);
        return frame < kFirstSingingFrame ? kFirstSingingFrame
                                          : (frame > kLastSingingFrame ? kLastSingingFrame : frame);
    }

    // `vowel` is only read while singing.
    int frame(bool singing, float vowel, uint64_t now_ms) {
        if (singing) {
            was_singing_ = true;
            return singing_frame(vowel);
        }
        if (was_singing_)
            restart_idle(now_ms);

        const uint64_t elapsed = now_ms >= idle_start_ms_ ? now_ms - idle_start_ms_ : 0;
        const uint64_t ticks = (elapsed + idle_offset_ms_) / kIdleTickMs;
        if (ticks < kIdleHoldTicks) {
            const bool blink = (ticks >= 14 && ticks < 17) || (ticks >= 31 && ticks < 34);
            return blink ? kIdleBlinkFrame : kIdleHoldFrame;
        }
        const uint64_t step = (ticks - kIdleHoldTicks) / kIdleShuffleTicksPerStep;
        return kIdleShuffle[step % (sizeof(kIdleShuffle) / sizeof(kIdleShuffle[0]))];
    }

private:
    uint64_t idle_offset_ms_ = 0;
    uint64_t idle_start_ms_ = 0;
    bool was_singing_ = false;
};

} // namespace delay_lama
