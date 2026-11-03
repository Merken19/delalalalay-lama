#pragma once

#include <windows.h>
#include <mmsystem.h>

#include "../Sources/standalone_bridge.h"

#include <algorithm>
#include <cstdint>

namespace delay_lama {

constexpr uint32_t kAudioFrames = 512;
constexpr uint32_t kAudioBuffers = 4;

// One output sample as the device gets it: clipped to -1..1, 16-bit.
inline int16_t pcm16_from_float(float sample) {
    return static_cast<int16_t>(std::max(-1.0f, std::min(1.0f, sample)) * 32767.0f);
}

// WinMM waveOut player: four 512-frame 48 kHz stereo buffers refilled from a
// dedicated thread whenever the device hands one back. It plays one bridge
// handle (the standalone) or anything a render callback mixes (the voice
// helper's choir).
class AudioEngine {
public:
    // Fills `frames` samples of left and right, which start zeroed.
    using RenderFn = void (*)(void *context, float *left, float *right, uint32_t frames);

    explicit AudioEngine(DelayLamaHandle *handle)
        : render_(&AudioEngine::render_handle), context_(handle) {}
    AudioEngine(RenderFn render, void *context) : render_(render), context_(context) {}

    ~AudioEngine() { stop(); }

    AudioEngine(const AudioEngine &) = delete;
    AudioEngine &operator=(const AudioEngine &) = delete;

    bool start() {
        if (!render_ || !context_ || wave_out_)
            return false;

        event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event_)
            return false;

        WAVEFORMATEX format = {};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = 2;
        format.nSamplesPerSec = 48000;
        format.wBitsPerSample = 16;
        format.nBlockAlign = static_cast<WORD>(format.nChannels * format.wBitsPerSample / 8);
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

        const MMRESULT open_result = waveOutOpen(
            &wave_out_, WAVE_MAPPER, &format,
            reinterpret_cast<DWORD_PTR>(&AudioEngine::wave_callback),
            reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION);
        if (open_result != MMSYSERR_NOERROR) {
            CloseHandle(event_);
            event_ = nullptr;
            wave_out_ = nullptr;
            return false;
        }

        prepared_headers_ = 0;
        for (uint32_t index = 0; index < kAudioBuffers; ++index) {
            WAVEHDR &header = headers_[index];
            ZeroMemory(&header, sizeof(header));
            header.lpData = reinterpret_cast<LPSTR>(pcm_[index]);
            header.dwBufferLength = sizeof(pcm_[index]);
            if (waveOutPrepareHeader(wave_out_, &header, sizeof(header)) != MMSYSERR_NOERROR) {
                stop();
                return false;
            }
            ++prepared_headers_;
        }

        InterlockedExchange(&running_, 1);
        for (uint32_t index = 0; index < kAudioBuffers; ++index) {
            fill_buffer(index);
            if (waveOutWrite(wave_out_, &headers_[index], sizeof(headers_[index])) !=
                MMSYSERR_NOERROR) {
                stop();
                return false;
            }
        }

        thread_ = CreateThread(nullptr, 0, &AudioEngine::audio_thread, this, 0, nullptr);
        if (!thread_) {
            stop();
            return false;
        }
        return true;
    }

    void stop() {
        InterlockedExchange(&running_, 0);
        if (event_)
            SetEvent(event_);
        if (thread_) {
            WaitForSingleObject(thread_, INFINITE);
            CloseHandle(thread_);
            thread_ = nullptr;
        }
        if (wave_out_) {
            waveOutReset(wave_out_);
            for (uint32_t index = 0; index < prepared_headers_; ++index)
                waveOutUnprepareHeader(wave_out_, &headers_[index], sizeof(headers_[index]));
            waveOutClose(wave_out_);
            wave_out_ = nullptr;
            prepared_headers_ = 0;
        }
        if (event_) {
            CloseHandle(event_);
            event_ = nullptr;
        }
    }

private:
    static void render_handle(void *context, float *left, float *right, uint32_t frames) {
        dl_process(static_cast<DelayLamaHandle *>(context), left, right, frames);
    }

    static void CALLBACK wave_callback(HWAVEOUT wave_out, UINT message, DWORD_PTR instance,
                                       DWORD_PTR param1, DWORD_PTR param2) {
        (void)wave_out;
        (void)param1;
        (void)param2;
        if (message == WOM_DONE) {
            AudioEngine *engine = reinterpret_cast<AudioEngine *>(instance);
            if (engine && engine->event_)
                SetEvent(engine->event_);
        }
    }

    static DWORD WINAPI audio_thread(LPVOID parameter) {
        AudioEngine *engine = reinterpret_cast<AudioEngine *>(parameter);
        engine->render_loop();
        return 0;
    }

    void fill_buffer(uint32_t index) {
        float left[kAudioFrames] = {};
        float right[kAudioFrames] = {};
        render_(context_, left, right, kAudioFrames);

        int16_t *destination = pcm_[index];
        for (uint32_t frame = 0; frame < kAudioFrames; ++frame) {
            destination[frame * 2] = pcm16_from_float(left[frame]);
            destination[frame * 2 + 1] = pcm16_from_float(right[frame]);
        }
    }

    void render_loop() {
        while (InterlockedCompareExchange(&running_, 0, 0) != 0) {
            const DWORD wait_result = WaitForSingleObject(event_, 1000);
            if (InterlockedCompareExchange(&running_, 0, 0) == 0)
                break;
            if (wait_result != WAIT_OBJECT_0 && wait_result != WAIT_TIMEOUT)
                break;

            for (uint32_t index = 0; index < kAudioBuffers; ++index) {
                WAVEHDR &header = headers_[index];
                if ((header.dwFlags & WHDR_DONE) == 0)
                    continue;
                fill_buffer(index);
                if (waveOutWrite(wave_out_, &header, sizeof(header)) != MMSYSERR_NOERROR) {
                    InterlockedExchange(&running_, 0);
                    break;
                }
            }
        }
    }

    RenderFn render_ = nullptr;
    void *context_ = nullptr;
    HWAVEOUT wave_out_ = nullptr;
    HANDLE event_ = nullptr;
    HANDLE thread_ = nullptr;
    volatile LONG running_ = 0;
    uint32_t prepared_headers_ = 0;
    WAVEHDR headers_[kAudioBuffers] = {};
    int16_t pcm_[kAudioBuffers][kAudioFrames * 2] = {};
};

} // namespace delay_lama
