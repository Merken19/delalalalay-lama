// DelayLamaVoice.exe: sings one MIDI channel with a choir of Delay Lama monks.
// It reads text commands from stdin (voice_protocol.h), mixes the monks into
// one waveOut stream and shows them side by side in one window. Delay Lama
// Everything starts one per Delay Lama channel; it can also be driven by hand
// from a console, or render a command script to a WAV file.

#include "voice_render.h"
#include "voice_window.h"

#include <cmath>
#include <cstdio>
#include <cwchar>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>

namespace delay_lama::voice {
namespace {

const char kUsage[] =
    "Usage:\n"
    "  DelayLamaVoice [options]                       sing the commands read from stdin\n"
    "  DelayLamaVoice --render <script|-> <out.wav> [--tail <ms>] [--monks <n>]\n"
    "\n"
    "Options:\n"
    "  --monks <1-8>          choir size, one monk per simultaneous note (default 4)\n"
    "  --x <px> --y <px>      window position (client area, physical pixels)\n"
    "  --width <px> --height <px>  window size (client area)\n"
    "  --topmost | --no-topmost      always on top (default on)\n"
    "  --title-bar | --no-title-bar  title bar (default on); without one the\n"
    "                         window drags to move and resizes at its edges\n"
    "  --title <text>         window title\n"
    "  --label <text>         a strip under the monks showing this text\n"
    "  --stretch              fill each monk's cell instead of keeping the aspect\n"
    "  --smooth               smooth scaling instead of nearest neighbour\n"
    "  --tail <ms>            --render: audio rendered after the script (default 1000)\n"
    "\n"
    "Commands, one per line (protocol 0):\n"
    "  hello 0 | on <note> <velocity> | off <note> | bend <0-16383> |\n"
    "  bendrange <semitones> | cc <number> <value> | program <bank> <program> |\n"
    "  allnotesoff | set <key> <value> | wait <ms> | quit\n"
    "The helper exits on quit, at the end of its input, or when its window closes.\n";

struct Options {
    int monks = kDefaultMonks;
    WindowOptions window;
    bool render = false;
    std::wstring script;
    std::wstring output;
    double tail_ms = 1000.0;
};

void write_handle(DWORD which, const std::string &text) {
    static std::mutex lock;
    std::lock_guard<std::mutex> guard(lock);
    HANDLE handle = GetStdHandle(which);
    DWORD written = 0;
    if (handle && handle != INVALID_HANDLE_VALUE)
        WriteFile(handle, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
}

void reply(const std::string &line) { write_handle(STD_OUTPUT_HANDLE, line + "\n"); }
void report(const std::string &text) { write_handle(STD_ERROR_HANDLE, text); }

bool parse_int(const wchar_t *text, int low, int high, int &value) {
    wchar_t *end = nullptr;
    const long parsed = std::wcstol(text, &end, 10);
    if (end == text || *end != L'\0' || parsed < low || parsed > high)
        return false;
    value = static_cast<int>(parsed);
    return true;
}

// Returns 0 to run, 1 after --help, 2 on a bad command line.
int parse_options(int argc, wchar_t **argv, Options &options) {
    bool has_x = false;
    bool has_y = false;
    for (int index = 1; index < argc; ++index) {
        const std::wstring option = argv[index];
        const bool has_value = index + 1 < argc;
        const wchar_t *value = has_value ? argv[index + 1] : L"";
        bool ok = true;
        if (option == L"--help" || option == L"-h" || option == L"/?") {
            report(kUsage);
            return 1;
        } else if (option == L"--render" && index + 2 < argc) {
            options.render = true;
            options.script = argv[index + 1];
            options.output = argv[index + 2];
            index += 2;
            continue;
        } else if (option == L"--monks" && has_value) {
            ok = parse_int(value, 1, VoiceAllocator::kMaxMonks, options.monks);
        } else if (option == L"--x" && has_value) {
            ok = parse_int(value, -32000, 32000, options.window.x);
            has_x = true;
        } else if (option == L"--y" && has_value) {
            ok = parse_int(value, -32000, 32000, options.window.y);
            has_y = true;
        } else if (option == L"--width" && has_value) {
            ok = parse_int(value, 1, 32000, options.window.width);
        } else if (option == L"--height" && has_value) {
            ok = parse_int(value, 1, 32000, options.window.height);
        } else if (option == L"--title" && has_value) {
            options.window.title = value;
        } else if (option == L"--label" && has_value) {
            options.window.label = value;
        } else if (option == L"--tail" && has_value) {
            int tail = 0;
            ok = parse_int(value, 0, 600000, tail);
            options.tail_ms = tail;
        } else if (option == L"--topmost" || option == L"--no-topmost") {
            options.window.topmost = option == L"--topmost";
            continue;
        } else if (option == L"--title-bar" || option == L"--no-title-bar") {
            options.window.title_bar = option == L"--title-bar";
            continue;
        } else if (option == L"--stretch") {
            options.window.keep_aspect = false;
            continue;
        } else if (option == L"--smooth") {
            options.window.smooth = true;
            continue;
        } else {
            ok = false;
        }
        if (!ok) {
            const int length = WideCharToMultiByte(CP_UTF8, 0, option.c_str(), -1, nullptr, 0,
                                                   nullptr, nullptr);
            std::string narrow(static_cast<size_t>(std::max(0, length - 1)), '\0');
            WideCharToMultiByte(CP_UTF8, 0, option.c_str(), -1, narrow.data(), length, nullptr,
                                nullptr);
            report("DelayLamaVoice: bad or incomplete option " + narrow + "\n\n" + kUsage);
            return 2;
        }
        ++index; // the option's value
    }
    if (has_x != has_y) {
        report("DelayLamaVoice: --x and --y go together\n");
        return 2;
    }
    options.window.has_position = has_x;
    return 0;
}

int run_render(const Options &options) {
    std::ifstream file;
    std::istream *script = &std::cin;
    if (options.script != L"-") {
        file.open(options.script.c_str());
        if (!file) {
            report("DelayLamaVoice: could not open the script\n");
            return 1;
        }
        script = &file;
    }
    RenderResult result;
    std::string error;
    if (!render_script(*script, options.monks, options.tail_ms, result, error) ||
        !write_wav(options.output, result.pcm, error)) {
        report("DelayLamaVoice: " + error + "\n");
        return 1;
    }
    char text[200];
    const double seconds = static_cast<double>(result.pcm.size() / 2) / kSampleRate;
    const double peak_db = result.peak > 0.0f ? 20.0 * std::log10(result.peak) : -999.0;
    snprintf(text, sizeof(text),
             "DelayLamaVoice: rendered %.2f s with %d monks, peak %.1f dBFS, %llu clipped "
             "samples\n",
             seconds, options.monks, peak_db, static_cast<unsigned long long>(result.clipped));
    report(text);
    return 0;
}

// State shared by the window thread and the command reader.
struct Session {
    Choir *choir = nullptr;
    HWND window = nullptr;
    std::mutex lock;
    bool closing = false;
    int exit_code = 0;
};

HWND g_window = nullptr;

BOOL WINAPI console_control(DWORD) {
    // Ctrl+C, Ctrl+Break, closing the console: close the window normally.
    if (g_window)
        PostMessageW(g_window, WM_CLOSE, 0, 0);
    return TRUE;
}

// False when the helper should stop reading.
bool handle_line(Session &session, const std::string &line) {
    if (!line.empty() && line[0] == '\x1a') // Ctrl+Z typed at a console
        return false;
    Command command;
    std::string error;
    switch (parse_line(line, command, error)) {
    case ParseResult::Blank:
        return true;
    case ParseResult::Error:
        reply("error " + error);
        return true;
    case ParseResult::Command:
        break;
    }
    switch (command.type) {
    case CommandType::Hello:
        if (command.a < kOldestProtocolVersion || command.a > kProtocolVersion) {
            reply("error protocol version " + std::to_string(command.a) +
                  " is not supported (this helper speaks " +
                  std::to_string(kOldestProtocolVersion) + " to " +
                  std::to_string(kProtocolVersion) + ")");
            session.exit_code = 2;
            return false;
        }
        // Answer in the version asked for: protocol 1 only adds to 0.
        reply("ready " + std::to_string(command.a));
        return true;
    case CommandType::Quit:
        return false;
    case CommandType::Wait:
        Sleep(static_cast<DWORD>(std::lround(command.value)));
        return true;
    default:
        break;
    }
    std::lock_guard<std::mutex> guard(session.lock);
    if (session.closing)
        return false;
    if (!session.choir->apply(command, error))
        reply("error " + error);
    return true;
}

DWORD WINAPI read_commands(LPVOID parameter) {
    Session &session = *static_cast<Session *>(parameter);
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    std::string pending;
    char buffer[4096];
    bool reading = true;
    while (reading) {
        DWORD count = 0;
        // Fails or reads nothing at the end of the input (a closed pipe).
        if (!ReadFile(input, buffer, sizeof(buffer), &count, nullptr) || count == 0)
            break;
        pending.append(buffer, count);
        size_t newline = 0;
        while (reading && (newline = pending.find('\n')) != std::string::npos) {
            const std::string line = pending.substr(0, newline);
            pending.erase(0, newline + 1);
            reading = handle_line(session, line);
        }
    }
    if (reading && !pending.empty())
        handle_line(session, pending);
    PostMessageW(session.window, WM_CLOSE, 0, 0);
    return 0;
}

// The monks' audio must not wait behind the 3D window or other helpers: a
// process whose window is off screen or covered can be throttled by Windows 11
// (EcoQoS, coarser timers), and a dozen helpers share the CPU with the
// renderer. Raise the priority a step and opt out of power throttling. Both
// are best effort.
void keep_audio_responsive() {
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    PROCESS_POWER_THROTTLING_STATE throttling = {};
    throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    throttling.ControlMask =
        PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    throttling.StateMask = 0; // never throttle
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &throttling,
                          sizeof(throttling));
}

int run_live(const Options &options) {
    enable_per_monitor_dpi_awareness();
    keep_audio_responsive();
    Gdiplus::GdiplusStartupInput gdiplus_input;
    ULONG_PTR gdiplus_token = 0;
    if (Gdiplus::GdiplusStartup(&gdiplus_token, &gdiplus_input, nullptr) != Gdiplus::Ok) {
        reply("error could not initialise Windows graphics");
        return 1;
    }

    // Static: the reader thread may still look at it while the process exits.
    static Session session;
    int exit_code = 1;
    {
        Choir choir(options.monks);
        FaceFrames faces;
        ChoirWindow window(choir, faces, options.window);
        AudioEngine audio(&Choir::render_callback, &choir);
        if (!choir.ok()) {
            reply("error could not create the monk synths");
        } else if (!faces.load(module_directory() + L"\\Resources\\faces.png")) {
            reply("error could not find Resources\\faces.png next to DelayLamaVoice.exe");
        } else if (!window.create(GetModuleHandleW(nullptr))) {
            reply("error could not create the monk window");
        } else if (!audio.start()) {
            reply("error could not open the default audio device");
        } else {
            session.choir = &choir;
            session.window = window.hwnd();
            g_window = window.hwnd();
            SetConsoleCtrlHandler(&console_control, TRUE);
            HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
            if (GetFileType(input) == FILE_TYPE_CHAR) {
                report("Delay Lama Voice: type one command per line, for example on 60 100, "
                       "off 60 or quit.\n");
            }
            // The reader is never joined: at exit it may still be blocked in
            // ReadFile. After `closing` it no longer touches the choir.
            HANDLE reader = CreateThread(nullptr, 0, &read_commands, &session, 0, nullptr);
            if (!reader) {
                reply("error could not start the command reader");
            } else {
                CloseHandle(reader);
                MSG message = {};
                while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
                exit_code = 0;
            }
            g_window = nullptr;
            audio.stop();
            std::lock_guard<std::mutex> guard(session.lock);
            session.closing = true;
            if (session.exit_code != 0)
                exit_code = session.exit_code;
        }
    }
    Gdiplus::GdiplusShutdown(gdiplus_token);
    return exit_code;
}

} // namespace
} // namespace delay_lama::voice

int wmain(int argc, wchar_t **argv) {
    using namespace delay_lama::voice;
    Options options;
    const int parsed = parse_options(argc, argv, options);
    if (parsed != 0)
        return parsed == 1 ? 0 : 2;
    return options.render ? run_render(options) : run_live(options);
}
