#include "app.h"

#include <commctrl.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <utility>

namespace delay_lama {
namespace {

// Layout is written in 96-DPI pixels and scaled to the window's DPI.
constexpr int kDesignWidth = 480;
constexpr int kDesignHeight = 500;
constexpr int kContentTop = 46;
constexpr int kRowHeight = 34;

enum Page { kPageVoice = 0, kPageEcho = 1, kPageFlag = 2, kPageWindows = 3, kAllPages = -1 };
const wchar_t *const kPageTitles[] = {L"Voice", L"Echo / Envelope", L"Flag", L"Windows"};

int to_slider(float value, float scale) { return static_cast<int>(std::lround(value * scale)); }

std::wstring format_text(const wchar_t *format, double value) {
    wchar_t text[32] = {};
    swprintf(text, ARRAYSIZE(text), format, value);
    return text;
}

std::wstring percent(int value) { return std::to_wstring(value) + L"%"; }
std::wstring milliseconds(int value) { return std::to_wstring(value) + L" ms"; }

const wchar_t kFlagHelp[] =
    L"Drag or touch anywhere on the flag: left/right sets the pitch, up/down sets "
    L"the vowel.\r\n\r\n"
    L"Computer keyboard (flag, monk or classic window focused): A W S E D F T G Y H U J K O L. "
    L"MIDI keyboards are picked up automatically at start-up.";

const wchar_t kClassicHelp[] =
    L"The classic window is the original Delay Lama panel in one resizable window: play the "
    L"flag, turn the left knob for glide and the right knob for the voice, slide the handle "
    L"under the flag for echo, and press ? for help.";

const wchar_t kWindowsHelp[] =
    L"X / Y / W / H set each window's inner area in screen pixels (Enter or Apply). "
    L"Without a title bar the flag cannot be moved, so every touch plays; the borderless "
    L"monk and classic windows move by dragging the picture and resize at their edges. "
    L"Right-click any window for quick options.";

} // namespace

SettingsWindow::~SettingsWindow() {
    if (font_)
        DeleteObject(font_);
    if (heading_font_)
        DeleteObject(heading_font_);
}

SIZE SettingsWindow::client_size_for_dpi(UINT dpi) {
    return {scale_px(kDesignWidth, dpi), scale_px(kDesignHeight, dpi)};
}

bool SettingsWindow::create() {
    if (!create_window(L"DelayLamaSettingsWindow", L"Delay Lama Settings",
                       WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
                       WS_EX_CONTROLPARENT, GetSysColorBrush(COLOR_WINDOW)))
        return false;
    build_controls();
    layout(dpi_for_window(hwnd_));
    show_page(kPageVoice);
    settings_changed();
    return true;
}

HWND SettingsWindow::add(int page, const wchar_t *class_name, const wchar_t *text, DWORD style,
                         int x, int y, int width, int height, bool heading) {
    const int id = 200 + static_cast<int>(controls_.size());
    HWND control = CreateWindowExW(0, class_name, text, WS_CHILD | WS_CLIPSIBLINGS | style, 0, 0,
                                   0, 0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                   app_.instance(), nullptr);
    controls_.push_back({control, page, adding_layout_, {x, y, x + width, y + height}, heading});
    return control;
}

void SettingsWindow::add_slider(int page, int row, const wchar_t *label, int minimum,
                                int maximum, std::function<int()> get,
                                std::function<void(int)> set,
                                std::function<std::wstring(int)> format) {
    const int y = kContentTop + row * kRowHeight;
    add(page, WC_STATICW, label, SS_LEFT | SS_NOPREFIX, 24, y + 5, 128, 20);
    HWND slider = add(page, TRACKBAR_CLASSW, L"",
                      WS_TABSTOP | TBS_HORZ | TBS_NOTICKS | TBS_FIXEDLENGTH, 154, y, 238, 28);
    HWND value = add(page, WC_STATICW, L"", SS_LEFT | SS_NOPREFIX, 398, y + 5, 66, 20);
    SendMessageW(slider, TBM_SETRANGEMIN, FALSE, minimum);
    SendMessageW(slider, TBM_SETRANGEMAX, FALSE, maximum);
    SendMessageW(slider, TBM_SETLINESIZE, 0, 1);
    SendMessageW(slider, TBM_SETPAGESIZE, 0, std::max(1, (maximum - minimum) / 10));
    sliders_.push_back({slider, value, std::move(get), std::move(set), std::move(format)});
}

void SettingsWindow::add_check(int page, int x, int y, int width, const wchar_t *label,
                               std::function<bool()> get, std::function<void(bool)> set,
                               DWORD style) {
    HWND check = add(page, WC_BUTTONW, label, style, x, y, width, 24);
    checks_.push_back({check, std::move(get), std::move(set)});
}

void SettingsWindow::add_window_group(int y, const wchar_t *heading, WindowId id) {
    App &app = app_;
    add(kPageWindows, WC_STATICW, heading, WS_GROUP | SS_LEFT | SS_NOPREFIX, 24, y, 300, 20, true);
    add_check(kPageWindows, 24, y + 22, 70, L"Show",
              [&app, id] { return app.settings().window(id).visible; },
              [&app, id](bool on) { app.show_window(id, on, false); });
    add_check(kPageWindows, 100, y + 22, 90, L"Title bar",
              [&app, id] { return app.settings().window(id).title_bar; },
              [&app, id](bool on) {
                  app.settings().window(id).title_bar = on;
                  app.apply_window(id);
              });
    add_check(kPageWindows, 196, y + 22, 120, L"Always on top",
              [&app, id] { return app.settings().window(id).topmost; },
              [&app, id](bool on) {
                  app.settings().window(id).topmost = on;
                  app.apply_window(id);
              });
    add_check(kPageWindows, 322, y + 22, 140, L"Keep aspect ratio",
              [&app, id] { return app.settings().window(id).keep_aspect; },
              [&app, id](bool on) {
                  app.settings().window(id).keep_aspect = on;
                  app.apply_window(id);
              });

    GeometryGroup group = {id, {}, nullptr};
    const wchar_t *const labels[] = {L"X", L"Y", L"W", L"H"};
    for (int index = 0; index < 4; ++index) {
        const int x = 24 + index * 88;
        add(kPageWindows, WC_STATICW, labels[index], SS_LEFT | SS_NOPREFIX, x, y + 56, 16, 20);
        group.edits[static_cast<size_t>(index)] =
            add(kPageWindows, WC_EDITW, L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, x + 18,
                y + 52, 62, 24);
    }
    group.apply =
        add(kPageWindows, WC_BUTTONW, L"Apply", WS_TABSTOP | BS_PUSHBUTTON, 380, y + 51, 82, 26);
    geometry_.push_back(group);
}

void SettingsWindow::build_controls() {
    App &app = app_;
    SoundSettings &sound = app.settings().sound;
    FlagSettings &flag = app.settings().flag;

    tab_ = add(kAllPages, WC_TABCONTROLW, L"", WS_TABSTOP, 8, 8, 464, 446);
    for (int page = 0; page < 4; ++page) {
        TCITEMW item = {};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t *>(kPageTitles[page]);
        SendMessageW(tab_, TCM_INSERTITEMW, static_cast<WPARAM>(page),
                     reinterpret_cast<LPARAM>(&item));
    }

    // Voice
    add_slider(kPageVoice, 0, L"Volume", 0, 100, [&sound] { return to_slider(sound.volume, 100); },
               [&app, &sound](int v) { sound.volume = v / 100.0f; app.apply_sound(); }, percent);
    add_slider(kPageVoice, 1, L"Glide", 0, 100, [&sound] { return to_slider(sound.glide, 100); },
               [&app, &sound](int v) { sound.glide = v / 100.0f; app.apply_sound(); }, percent);
    add_slider(kPageVoice, 2, L"Voice (deep - high)", 0, 100,
               [&sound] { return to_slider(sound.voice, 100); },
               [&app, &sound](int v) { sound.voice = v / 100.0f; app.apply_sound(); }, percent);
    add_slider(kPageVoice, 3, L"Vibrato depth", 0, 100,
               [&sound] { return to_slider(sound.vibrato, 100); },
               [&app, &sound](int v) { sound.vibrato = v / 100.0f; app.apply_sound(); }, percent);
    add_slider(kPageVoice, 4, L"Vibrato speed", 0, 100,
               [&sound] { return to_slider(sound.vibrato_rate, 100); },
               [&app, &sound](int v) { sound.vibrato_rate = v / 100.0f; app.apply_sound(); },
               [](int v) {
                   return format_text(L"×%.2f", std::pow(4.0, v / 50.0 - 1.0));
               });
    add_slider(kPageVoice, 5, L"Breath", 0, 100, [&sound] { return to_slider(sound.breath, 100); },
               [&app, &sound](int v) { sound.breath = v / 100.0f; app.apply_sound(); }, percent);
    add_slider(kPageVoice, 6, L"Unison voices", 1, 10, [&sound] { return sound.unison; },
               [&app, &sound](int v) { sound.unison = v; app.apply_sound(); },
               [](int v) { return std::to_wstring(v); });
    add_slider(kPageVoice, 7, L"Unison detune", 0, 50,
               [&sound] { return to_slider(sound.unison_detune, 1); },
               [&app, &sound](int v) { sound.unison_detune = static_cast<float>(v); app.apply_sound(); },
               [](int v) { return std::to_wstring(v) + L" cents"; });
    add_slider(kPageVoice, 8, L"Unison spread", 0, 100,
               [&sound] { return to_slider(sound.unison_spread, 100); },
               [&app, &sound](int v) { sound.unison_spread = v / 100.0f; app.apply_sound(); },
               percent);
    HWND reset_voice = add(kPageVoice, WC_BUTTONW, L"Reset sound to defaults",
                           WS_TABSTOP | BS_PUSHBUTTON, 24, kContentTop + 9 * kRowHeight + 8, 190, 28);
    buttons_.emplace_back(reset_voice, [&app] { app.reset_sound(); });

    // Echo / Envelope
    add_slider(kPageEcho, 0, L"Echo amount", 0, 100,
               [&sound] { return to_slider(sound.delay_mix, 100); },
               [&app, &sound](int v) { sound.delay_mix = v / 100.0f; app.apply_sound(); }, percent);
    add_slider(kPageEcho, 1, L"Echo time", 0, 100,
               [&sound] { return to_slider(sound.delay_time, 100); },
               [&app, &sound](int v) { sound.delay_time = v / 100.0f; app.apply_sound(); },
               [](int v) {
                   // Left tap: 13653 samples at 44.1 kHz scaled by 0.1 + 1.9 * time.
                   const double seconds = 13653.0 / 44100.0 * (0.1 + 1.9 * v / 100.0);
                   return milliseconds(static_cast<int>(std::lround(seconds * 1000.0)));
               });
    add_slider(kPageEcho, 2, L"Echo feedback", 0, 95,
               [&sound] { return to_slider(sound.delay_feedback, 100); },
               [&app, &sound](int v) { sound.delay_feedback = v / 100.0f; app.apply_sound(); },
               percent);
    add_slider(kPageEcho, 3, L"Attack", 0, 200, [&sound] { return to_slider(sound.attack, 100); },
               [&app, &sound](int v) { sound.attack = v / 100.0f; app.apply_sound(); },
               [](int v) { return milliseconds(v * 10); });
    add_slider(kPageEcho, 4, L"Decay", 0, 200, [&sound] { return to_slider(sound.decay, 100); },
               [&app, &sound](int v) { sound.decay = v / 100.0f; app.apply_sound(); },
               [](int v) { return milliseconds(v * 10); });
    add_slider(kPageEcho, 5, L"Sustain", 0, 100, [&sound] { return to_slider(sound.sustain, 100); },
               [&app, &sound](int v) { sound.sustain = v / 100.0f; app.apply_sound(); }, percent);
    add_slider(kPageEcho, 6, L"Release", 0, 200, [&sound] { return to_slider(sound.release, 100); },
               [&app, &sound](int v) { sound.release = v / 100.0f; app.apply_sound(); },
               [](int v) { return milliseconds(v * 10); });
    HWND reset_echo = add(kPageEcho, WC_BUTTONW, L"Reset sound to defaults",
                          WS_TABSTOP | BS_PUSHBUTTON, 24, kContentTop + 7 * kRowHeight + 8, 190, 28);
    buttons_.emplace_back(reset_echo, [&app] { app.reset_sound(); });

    // Flag
    auto note_label = [](int v) { return note_name(v) + L" (" + std::to_wstring(v) + L")"; };
    add_slider(kPageFlag, 0, L"Lowest note (left)", 24, 107, [&flag] { return flag.low_note; },
               [this, &app, &flag](int v) {
                   flag.low_note = v;
                   if (flag.high_note <= v)
                       flag.high_note = v + 1;
                   app.apply_flag();
                   settings_changed();
               },
               note_label);
    add_slider(kPageFlag, 1, L"Highest note (right)", 25, 108, [&flag] { return flag.high_note; },
               [this, &app, &flag](int v) {
                   flag.high_note = v;
                   if (flag.low_note >= v)
                       flag.low_note = v - 1;
                   app.apply_flag();
                   settings_changed();
               },
               note_label);
    const int check_top = kContentTop + 2 * kRowHeight + 8;
    add_check(kPageFlag, 24, check_top, 420, L"Snap pitch to whole semitones",
              [&flag] { return flag.snap; },
              [&app, &flag](bool on) { flag.snap = on; app.apply_flag(); });
    add_check(kPageFlag, 24, check_top + 28, 420, L"Show note grid on the flag",
              [&flag] { return flag.show_grid; },
              [&app, &flag](bool on) { flag.show_grid = on; app.apply_flag(); });
    add_check(kPageFlag, 24, check_top + 56, 420, L"Show crosshair while playing",
              [&flag] { return flag.show_crosshair; },
              [&app, &flag](bool on) { flag.show_crosshair = on; app.apply_flag(); });
    add(kPageFlag, WC_STATICW, kFlagHelp, SS_LEFT | SS_NOPREFIX, 24, check_top + 98, 432, 200);

    // Windows: the window groups below the display choice follow the display.
    add(kPageWindows, WC_STATICW, L"Display", SS_LEFT | SS_NOPREFIX, 24, kContentTop, 300, 20,
        true);
    add_check(kPageWindows, 24, kContentTop + 22, 226, L"Separate flag and monk windows",
              [&app] { return !app.settings().classic; },
              [&app](bool on) {
                  if (on)
                      app.set_classic(false);
              },
              WS_TABSTOP | WS_GROUP | BS_AUTORADIOBUTTON);
    add_check(kPageWindows, 256, kContentTop + 22, 200, L"Classic single window",
              [&app] { return app.settings().classic; },
              [&app](bool on) {
                  if (on)
                      app.set_classic(true);
              },
              BS_AUTORADIOBUTTON);
    const int group_top = kContentTop + 58;
    adding_layout_ = 0;
    add_window_group(group_top, L"Flag window", WindowId::Flag);
    add_window_group(group_top + 92, L"Monk window", WindowId::Monk);
    adding_layout_ = 1;
    add_window_group(group_top, L"Classic window", WindowId::Classic);
    add(kPageWindows, WC_STATICW, kClassicHelp, SS_LEFT | SS_NOPREFIX, 24, group_top + 92, 432, 78);
    adding_layout_ = -1;

    const int general_top = group_top + 186;
    add(kPageWindows, WC_STATICW, L"General", WS_GROUP | SS_LEFT | SS_NOPREFIX, 24, general_top,
        300, 20, true);
    add_check(kPageWindows, 24, general_top + 22, 226, L"Settings window always on top",
              [&app] { return app.settings().window(WindowId::Settings).topmost; },
              [&app](bool on) {
                  app.settings().window(WindowId::Settings).topmost = on;
                  app.apply_window(WindowId::Settings);
              });
    add_check(kPageWindows, 256, general_top + 22, 200, L"Smooth image scaling",
              [&app] { return app.settings().smooth_scaling; },
              [&app](bool on) {
                  app.settings().smooth_scaling = on;
                  app.apply_scaling();
              });
    HWND reset_layout = add(kPageWindows, WC_BUTTONW, L"Reset window layout",
                            WS_TABSTOP | BS_PUSHBUTTON, 24, general_top + 54, 170, 28);
    buttons_.emplace_back(reset_layout, [&app] { app.reset_layout(); });
    add(kPageWindows, WC_STATICW, kWindowsHelp, SS_LEFT | SS_NOPREFIX, 24, general_top + 94, 432,
        70);

    // Every page
    HWND about = add(kAllPages, WC_BUTTONW, L"About / manual...", WS_TABSTOP | BS_PUSHBUTTON, 8,
                     462, 150, 28);
    buttons_.emplace_back(about, [this] { app_.show_about(hwnd_); });
    HWND reset_all = add(kAllPages, WC_BUTTONW, L"Reset all settings...",
                         WS_TABSTOP | BS_PUSHBUTTON, 165, 462, 150, 28);
    buttons_.emplace_back(reset_all, [this] { app_.reset_all(hwnd_); });
    HWND exit = add(kAllPages, WC_BUTTONW, L"Exit Delay Lama", WS_TABSTOP | BS_PUSHBUTTON, 322,
                    462, 150, 28);
    buttons_.emplace_back(exit, [&app] { app.quit(); });
}

void SettingsWindow::update_fonts(UINT dpi) {
    using SystemParametersInfoForDpiFn = BOOL(WINAPI *)(UINT, UINT, PVOID, UINT, UINT);
    static const auto system_parameters_for_dpi = reinterpret_cast<SystemParametersInfoForDpiFn>(
        user32_proc("SystemParametersInfoForDpi"));

    NONCLIENTMETRICSW metrics = {};
    metrics.cbSize = sizeof(metrics);
    LOGFONTW font = {};
    if (system_parameters_for_dpi &&
        system_parameters_for_dpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi)) {
        font = metrics.lfMessageFont;
    } else if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
        font = metrics.lfMessageFont;
        font.lfHeight = MulDiv(font.lfHeight, static_cast<int>(dpi),
                               static_cast<int>(dpi_for_window(nullptr)));
    } else {
        font.lfHeight = -MulDiv(9, static_cast<int>(dpi), 72);
        wcscpy_s(font.lfFaceName, L"Segoe UI");
    }
    font_ = CreateFontIndirectW(&font);
    font.lfWeight = FW_SEMIBOLD;
    heading_font_ = CreateFontIndirectW(&font);
}

void SettingsWindow::layout(UINT dpi) {
    HFONT old_font = font_;
    HFONT old_heading_font = heading_font_;
    update_fonts(dpi);

    HDWP positions = BeginDeferWindowPos(static_cast<int>(controls_.size()));
    for (const Control &control : controls_) {
        const int x = scale_px(control.design.left, dpi);
        const int y = scale_px(control.design.top, dpi);
        const int width = scale_px(control.design.right, dpi) - x;
        const int height = scale_px(control.design.bottom, dpi) - y;
        if (positions)
            positions = DeferWindowPos(positions, control.hwnd, nullptr, x, y, width, height,
                                       SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (positions)
        EndDeferWindowPos(positions);

    for (const Control &control : controls_)
        SendMessageW(control.hwnd, WM_SETFONT,
                     reinterpret_cast<WPARAM>(control.heading ? heading_font_ : font_), FALSE);
    for (const Slider &slider : sliders_)
        SendMessageW(slider.slider, TBM_SETTHUMBLENGTH, static_cast<WPARAM>(scale_px(20, dpi)), 0);
    // Only the tab strip is shown: the themed pane is not quite the window
    // colour, so page controls sit on the plain window background instead.
    RECT tab_rect = {0, 0, scale_px(464, dpi), scale_px(446, dpi)};
    SendMessageW(tab_, TCM_ADJUSTRECT, FALSE, reinterpret_cast<LPARAM>(&tab_rect));
    SetWindowPos(tab_, HWND_BOTTOM, 0, 0, scale_px(464, dpi),
                 std::max(1L, tab_rect.top - scale_px(2, dpi)), SWP_NOMOVE | SWP_NOACTIVATE);
    InvalidateRect(hwnd_, nullptr, TRUE);

    if (old_font)
        DeleteObject(old_font);
    if (old_heading_font)
        DeleteObject(old_heading_font);
}

void SettingsWindow::show_page(int page) {
    page_ = page;
    shown_classic_ = app_.settings().classic;
    const int layout = shown_classic_ ? 1 : 0;
    SendMessageW(tab_, TCM_SETCURSEL, static_cast<WPARAM>(page), 0);
    for (const Control &control : controls_) {
        const bool on_page = control.page == kAllPages || control.page == page;
        const bool in_layout = control.layout < 0 || control.layout == layout;
        ShowWindow(control.hwnd, on_page && in_layout ? SW_SHOW : SW_HIDE);
    }
    // Keyboard focus must not stay on a control that just disappeared.
    HWND focus = GetFocus();
    if (focus && GetParent(focus) == hwnd_ && !IsWindowVisible(focus))
        SetFocus(tab_);
}

void SettingsWindow::settings_changed() {
    if (!hwnd_)
        return;
    if (app_.settings().classic != shown_classic_)
        show_page(page_);
    for (const Slider &slider : sliders_) {
        const int value = slider.get();
        SendMessageW(slider.slider, TBM_SETPOS, TRUE, value);
        SetWindowTextW(slider.value, slider.format(value).c_str());
    }
    for (const Check &check : checks_)
        SendMessageW(check.hwnd, BM_SETCHECK, check.get() ? BST_CHECKED : BST_UNCHECKED, 0);
    for (const GeometryGroup &group : geometry_)
        refresh_geometry(group.id);
}

// Leaves the fields alone while the user is typing in them.
void SettingsWindow::refresh_geometry(WindowId id) {
    if (!hwnd_)
        return;
    HWND focus = GetFocus();
    for (const GeometryGroup &group : geometry_) {
        if (group.id != id)
            continue;
        for (HWND edit : group.edits) {
            if (edit == focus)
                return;
        }
        const WindowSettings &window = app_.settings().window(id);
        const int values[] = {window.x, window.y, window.width, window.height};
        for (size_t index = 0; index < 4; ++index)
            SetWindowTextW(group.edits[index], std::to_wstring(values[index]).c_str());
    }
}

void SettingsWindow::apply_geometry(const GeometryGroup &group) {
    int values[4] = {};
    for (size_t index = 0; index < 4; ++index) {
        wchar_t text[32] = {};
        GetWindowTextW(group.edits[index], text, ARRAYSIZE(text));
        wchar_t *end = nullptr;
        const long value = std::wcstol(text, &end, 10);
        if (end == text) {
            MessageBeep(MB_ICONWARNING);
            SetFocus(group.edits[index]);
            return;
        }
        values[index] = static_cast<int>(std::max(-32000L, std::min(32000L, value)));
    }
    // Move focus off the fields so they show the geometry Windows applied.
    SetFocus(group.apply);
    app_.set_window_geometry(group.id, values[0], values[1], values[2], values[3]);
}

void SettingsWindow::on_command(HWND control, UINT code, UINT id) {
    if (id == IDOK) {
        // Enter in a geometry field (IsDialogMessage sends IDOK).
        HWND focus = GetFocus();
        for (const GeometryGroup &group : geometry_) {
            for (HWND edit : group.edits) {
                if (edit == focus) {
                    apply_geometry(group);
                    return;
                }
            }
        }
        return;
    }
    if (code != BN_CLICKED || !control)
        return;
    for (const Check &check : checks_) {
        if (check.hwnd == control) {
            check.set(SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED);
            return;
        }
    }
    for (const GeometryGroup &group : geometry_) {
        if (group.apply == control) {
            apply_geometry(group);
            return;
        }
    }
    for (const auto &button : buttons_) {
        if (button.first == control) {
            button.second();
            return;
        }
    }
}

void SettingsWindow::on_scroll(HWND control) {
    for (const Slider &slider : sliders_) {
        if (slider.slider != control)
            continue;
        const int value = static_cast<int>(SendMessageW(control, TBM_GETPOS, 0, 0));
        slider.set(value);
        SetWindowTextW(slider.value, slider.format(value).c_str());
        return;
    }
}

LRESULT SettingsWindow::handle_message(UINT message, WPARAM w_param, LPARAM l_param) {
    switch (message) {
    case WM_CLOSE:
        app_.close_window(id_);
        return 0;
    case WM_MOVE:
        app_.window_geometry_changed(id_);
        return 0;
    case WM_HSCROLL:
        on_scroll(reinterpret_cast<HWND>(l_param));
        return 0;
    case WM_COMMAND:
        on_command(reinterpret_cast<HWND>(l_param), HIWORD(w_param), LOWORD(w_param));
        return 0;
    case WM_NOTIFY: {
        const NMHDR *header = reinterpret_cast<const NMHDR *>(l_param);
        if (header->hwndFrom == tab_ && header->code == static_cast<UINT>(TCN_SELCHANGE)) {
            show_page(static_cast<int>(SendMessageW(tab_, TCM_GETCURSEL, 0, 0)));
            return 0;
        }
        break;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC dc = reinterpret_cast<HDC>(w_param);
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        SetBkColor(dc, GetSysColor(COLOR_WINDOW));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    case WM_DPICHANGED: {
        const UINT dpi = HIWORD(w_param);
        const RECT *suggested = reinterpret_cast<const RECT *>(l_param);
        const SIZE client = client_size_for_dpi(dpi);
        RECT rect = {0, 0, client.cx, client.cy};
        adjust_window_rect_for_dpi(rect, static_cast<DWORD>(GetWindowLongW(hwnd_, GWL_STYLE)),
                                   static_cast<DWORD>(GetWindowLongW(hwnd_, GWL_EXSTYLE)), dpi);
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, rect.right - rect.left,
                     rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE);
        layout(dpi);
        return 0;
    }
    case WM_ENDSESSION:
        if (w_param)
            app_.save();
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, w_param, l_param);
}

} // namespace delay_lama
