#pragma once

#include <cstdint>
#include <istream>
#include <string>
#include <vector>

namespace delay_lama::voice {

struct RenderResult {
    std::vector<int16_t> pcm; // interleaved stereo, 48 kHz, as the device would get it
    float peak = 0.0f;        // largest sample before clipping
    uint64_t clipped = 0;     // samples beyond -1..1
};

// Plays a command script into a choir of `monks` with no window and no audio
// device. Commands take effect at the exact sample that the `wait`s before
// them reach; after the script (or a quit), `tail_ms` more is rendered. The
// output depends only on the script and the options. False on the first bad
// line, with `error` naming it.
bool render_script(std::istream &script, int monks, double tail_ms, RenderResult &result,
                   std::string &error);

// 16-bit stereo PCM WAV at 48 kHz.
bool write_wav(const std::wstring &path, const std::vector<int16_t> &pcm, std::string &error);

} // namespace delay_lama::voice
