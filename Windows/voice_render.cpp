#include "voice_render.h"

#include "audio_engine.h"
#include "voice_choir.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace delay_lama::voice {
namespace {

void render_until(Choir &choir, uint64_t target_frames, uint64_t &rendered,
                  RenderResult &result) {
    float left[kAudioFrames];
    float right[kAudioFrames];
    while (rendered < target_frames) {
        const uint32_t count =
            static_cast<uint32_t>(std::min<uint64_t>(kAudioFrames, target_frames - rendered));
        choir.render(left, right, count);
        for (uint32_t frame = 0; frame < count; ++frame) {
            for (const float sample : {left[frame], right[frame]}) {
                const float magnitude = std::fabs(sample);
                result.peak = std::max(result.peak, magnitude);
                if (magnitude > 1.0f)
                    ++result.clipped;
                result.pcm.push_back(pcm16_from_float(sample));
            }
        }
        rendered += count;
    }
}

uint64_t frames_at(double milliseconds) {
    return static_cast<uint64_t>(std::llround(milliseconds * kSampleRate / 1000.0));
}

void put_u32(std::vector<uint8_t> &out, uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<uint8_t>(value >> shift));
}

void put_u16(std::vector<uint8_t> &out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
}

} // namespace

bool render_script(std::istream &script, int monks, double tail_ms, RenderResult &result,
                   std::string &error) {
    result = RenderResult();
    Choir choir(monks);
    if (!choir.ok()) {
        error = "could not create the monk synths";
        return false;
    }

    double time_ms = 0.0;
    uint64_t rendered = 0;
    std::string line;
    int line_number = 0;
    while (std::getline(script, line)) {
        ++line_number;
        Command command;
        std::string problem;
        const ParseResult parsed = parse_line(line, command, problem);
        if (parsed == ParseResult::Blank)
            continue;
        if (parsed == ParseResult::Command && command.type == CommandType::Hello &&
            (command.a < kOldestProtocolVersion || command.a > kProtocolVersion))
            problem = "protocol version " + std::to_string(command.a) + " is not supported";
        if (parsed == ParseResult::Error || !problem.empty() ||
            !choir.apply(command, problem)) {
            error = "line " + std::to_string(line_number) + ": " + problem;
            return false;
        }
        if (command.type == CommandType::Wait) {
            time_ms += command.value;
            render_until(choir, frames_at(time_ms), rendered, result);
        } else if (command.type == CommandType::Quit) {
            break;
        }
    }
    render_until(choir, frames_at(time_ms + std::max(0.0, tail_ms)), rendered, result);
    return true;
}

bool write_wav(const std::wstring &path, const std::vector<int16_t> &pcm, std::string &error) {
    const uint32_t data_bytes = static_cast<uint32_t>(pcm.size() * sizeof(int16_t));
    const uint32_t rate = static_cast<uint32_t>(kSampleRate);
    std::vector<uint8_t> header;
    header.insert(header.end(), {'R', 'I', 'F', 'F'});
    put_u32(header, 36 + data_bytes);
    header.insert(header.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put_u32(header, 16);
    put_u16(header, 1); // PCM
    put_u16(header, 2); // stereo
    put_u32(header, rate);
    put_u32(header, rate * 4);
    put_u16(header, 4);
    put_u16(header, 16);
    header.insert(header.end(), {'d', 'a', 't', 'a'});
    put_u32(header, data_bytes);

    FILE *file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || !file) {
        error = "could not create the WAV file";
        return false;
    }
    // WAV samples are little-endian, as on every Windows machine.
    const bool written = fwrite(header.data(), 1, header.size(), file) == header.size() &&
                         fwrite(pcm.data(), 1, data_bytes, file) == data_bytes;
    const bool closed = fclose(file) == 0;
    if (!written || !closed) {
        error = "could not write the WAV file";
        return false;
    }
    return true;
}

} // namespace delay_lama::voice
