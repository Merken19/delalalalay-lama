#include "app.h"

#include <cmath>
#include <cstdlib>
#include <cwchar>

namespace delay_lama {
namespace {

const wchar_t *const kWindowSections[kWindowCount] = {L"FlagWindow", L"MonkWindow",
                                                      L"SettingsWindow", L"ClassicWindow"};

// %APPDATA%\DelayLamaStandalone\settings.ini, unless a settings.ini next to
// the exe opts into portable mode.
std::wstring settings_path() {
    const std::wstring portable = module_directory() + L"\\settings.ini";
    if (file_exists(portable))
        return portable;
    wchar_t app_data[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableW(L"APPDATA", app_data, ARRAYSIZE(app_data));
    if (length == 0 || length >= ARRAYSIZE(app_data))
        return portable;
    const std::wstring directory = std::wstring(app_data) + L"\\DelayLamaStandalone";
    CreateDirectoryW(directory.c_str(), nullptr);
    return directory + L"\\settings.ini";
}

class IniFile {
public:
    explicit IniFile(std::wstring path) : path_(std::move(path)) {}

    float read_float(const wchar_t *section, const wchar_t *key, float fallback, float minimum,
                     float maximum) const {
        std::wstring text;
        if (!read(section, key, text))
            return fallback;
        wchar_t *end = nullptr;
        const float value = std::wcstof(text.c_str(), &end);
        if (end == text.c_str() || !std::isfinite(value))
            return fallback;
        return value < minimum ? minimum : (value > maximum ? maximum : value);
    }

    int read_int(const wchar_t *section, const wchar_t *key, int fallback, int minimum,
                 int maximum) const {
        std::wstring text;
        if (!read(section, key, text))
            return fallback;
        wchar_t *end = nullptr;
        const long value = std::wcstol(text.c_str(), &end, 10);
        if (end == text.c_str())
            return fallback;
        return value < minimum ? minimum : (value > maximum ? maximum : static_cast<int>(value));
    }

    bool read_bool(const wchar_t *section, const wchar_t *key, bool fallback) const {
        return read_int(section, key, fallback ? 1 : 0, 0, 1) != 0;
    }

    void write_float(const wchar_t *section, const wchar_t *key, float value) const {
        wchar_t text[32] = {};
        swprintf(text, ARRAYSIZE(text), L"%.4f", static_cast<double>(value));
        WritePrivateProfileStringW(section, key, text, path_.c_str());
    }

    void write_int(const wchar_t *section, const wchar_t *key, int value) const {
        WritePrivateProfileStringW(section, key, std::to_wstring(value).c_str(), path_.c_str());
    }

    void write_bool(const wchar_t *section, const wchar_t *key, bool value) const {
        write_int(section, key, value ? 1 : 0);
    }

private:
    bool read(const wchar_t *section, const wchar_t *key, std::wstring &text) const {
        wchar_t buffer[64] = {};
        const DWORD length = GetPrivateProfileStringW(section, key, L"", buffer,
                                                      ARRAYSIZE(buffer), path_.c_str());
        if (length == 0)
            return false;
        text.assign(buffer, length);
        return true;
    }

    std::wstring path_;
};

} // namespace

Settings default_settings() {
    Settings settings;
    settings.window(WindowId::Monk).keep_aspect = true;
    settings.window(WindowId::Classic).keep_aspect = true;
    return settings;
}

void load_settings(Settings &settings) {
    settings = default_settings();
    const IniFile ini(settings_path());

    SoundSettings &sound = settings.sound;
    const wchar_t *section = L"Sound";
    sound.volume = ini.read_float(section, L"Volume", sound.volume, 0.0f, 1.0f);
    sound.glide = ini.read_float(section, L"Glide", sound.glide, 0.0f, 1.0f);
    sound.voice = ini.read_float(section, L"Voice", sound.voice, 0.0f, 1.0f);
    sound.vibrato = ini.read_float(section, L"Vibrato", sound.vibrato, 0.0f, 1.0f);
    sound.vibrato_rate = ini.read_float(section, L"VibratoRate", sound.vibrato_rate, 0.0f, 1.0f);
    sound.breath = ini.read_float(section, L"Breath", sound.breath, 0.0f, 1.0f);
    sound.unison = ini.read_int(section, L"UnisonVoices", sound.unison, 1, 10);
    sound.unison_detune = ini.read_float(section, L"UnisonDetune", sound.unison_detune, 0.0f, 50.0f);
    sound.unison_spread = ini.read_float(section, L"UnisonSpread", sound.unison_spread, 0.0f, 1.0f);
    sound.delay_mix = ini.read_float(section, L"EchoAmount", sound.delay_mix, 0.0f, 1.0f);
    sound.delay_time = ini.read_float(section, L"EchoTime", sound.delay_time, 0.0f, 1.0f);
    sound.delay_feedback =
        ini.read_float(section, L"EchoFeedback", sound.delay_feedback, 0.0f, 0.95f);
    sound.attack = ini.read_float(section, L"Attack", sound.attack, 0.0f, 2.0f);
    sound.decay = ini.read_float(section, L"Decay", sound.decay, 0.0f, 2.0f);
    sound.sustain = ini.read_float(section, L"Sustain", sound.sustain, 0.0f, 1.0f);
    sound.release = ini.read_float(section, L"Release", sound.release, 0.0f, 2.0f);

    FlagSettings &flag = settings.flag;
    section = L"Flag";
    flag.low_note = ini.read_int(section, L"LowNote", flag.low_note, 24, 107);
    flag.high_note = ini.read_int(section, L"HighNote", flag.high_note, 25, 108);
    if (flag.high_note <= flag.low_note)
        flag.high_note = flag.low_note + 1;
    flag.snap = ini.read_bool(section, L"SnapToSemitones", flag.snap);
    flag.show_grid = ini.read_bool(section, L"ShowNoteGrid", flag.show_grid);
    flag.show_crosshair = ini.read_bool(section, L"ShowCrosshair", flag.show_crosshair);

    settings.classic = ini.read_bool(L"Display", L"Classic", settings.classic);
    settings.smooth_scaling = ini.read_bool(L"Display", L"SmoothScaling", settings.smooth_scaling);

    for (int index = 0; index < kWindowCount; ++index) {
        WindowSettings &window = settings.windows[static_cast<size_t>(index)];
        section = kWindowSections[index];
        window.visible = ini.read_bool(section, L"Visible", window.visible);
        window.title_bar = ini.read_bool(section, L"TitleBar", window.title_bar);
        window.topmost = ini.read_bool(section, L"AlwaysOnTop", window.topmost);
        window.keep_aspect = ini.read_bool(section, L"KeepAspectRatio", window.keep_aspect);
        window.x = ini.read_int(section, L"X", window.x, -32000, 32000);
        window.y = ini.read_int(section, L"Y", window.y, -32000, 32000);
        window.width = ini.read_int(section, L"Width", window.width, 0, 32000);
        window.height = ini.read_int(section, L"Height", window.height, 0, 32000);
    }
    // The settings window always keeps its frame.
    settings.window(WindowId::Settings).title_bar = true;
}

void save_settings(const Settings &settings) {
    const IniFile ini(settings_path());

    const SoundSettings &sound = settings.sound;
    const wchar_t *section = L"Sound";
    ini.write_float(section, L"Volume", sound.volume);
    ini.write_float(section, L"Glide", sound.glide);
    ini.write_float(section, L"Voice", sound.voice);
    ini.write_float(section, L"Vibrato", sound.vibrato);
    ini.write_float(section, L"VibratoRate", sound.vibrato_rate);
    ini.write_float(section, L"Breath", sound.breath);
    ini.write_int(section, L"UnisonVoices", sound.unison);
    ini.write_float(section, L"UnisonDetune", sound.unison_detune);
    ini.write_float(section, L"UnisonSpread", sound.unison_spread);
    ini.write_float(section, L"EchoAmount", sound.delay_mix);
    ini.write_float(section, L"EchoTime", sound.delay_time);
    ini.write_float(section, L"EchoFeedback", sound.delay_feedback);
    ini.write_float(section, L"Attack", sound.attack);
    ini.write_float(section, L"Decay", sound.decay);
    ini.write_float(section, L"Sustain", sound.sustain);
    ini.write_float(section, L"Release", sound.release);

    const FlagSettings &flag = settings.flag;
    section = L"Flag";
    ini.write_int(section, L"LowNote", flag.low_note);
    ini.write_int(section, L"HighNote", flag.high_note);
    ini.write_bool(section, L"SnapToSemitones", flag.snap);
    ini.write_bool(section, L"ShowNoteGrid", flag.show_grid);
    ini.write_bool(section, L"ShowCrosshair", flag.show_crosshair);

    ini.write_bool(L"Display", L"Classic", settings.classic);
    ini.write_bool(L"Display", L"SmoothScaling", settings.smooth_scaling);

    for (int index = 0; index < kWindowCount; ++index) {
        const WindowSettings &window = settings.windows[static_cast<size_t>(index)];
        section = kWindowSections[index];
        ini.write_bool(section, L"Visible", window.visible);
        ini.write_bool(section, L"TitleBar", window.title_bar);
        ini.write_bool(section, L"AlwaysOnTop", window.topmost);
        ini.write_bool(section, L"KeepAspectRatio", window.keep_aspect);
        ini.write_int(section, L"X", window.x);
        ini.write_int(section, L"Y", window.y);
        ini.write_int(section, L"Width", window.width);
        ini.write_int(section, L"Height", window.height);
    }
}

} // namespace delay_lama
