#include "app.h"
#include "resource.h"

#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <string>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "winmm.lib")
// Themed (v6) common controls for the settings window.
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace delay_lama {
namespace {

constexpr UINT kMenuSettings = 1001;
constexpr UINT kMenuAbout = 1002;
constexpr UINT kMenuShowFlag = 1003;
constexpr UINT kMenuShowMonk = 1004;
constexpr UINT kMenuTitleBar = 1005;
constexpr UINT kMenuTopmost = 1006;
constexpr UINT kMenuKeepAspect = 1007;
constexpr UINT kMenuExit = 1008;
constexpr UINT kMenuClassic = 1009;
// Window-menu commands must stay below 0xF000 with the low four bits clear.
constexpr UINT kSysCommandSettings = 0x0010;
constexpr UINT kSysCommandAbout = 0x0020;

const wchar_t kAppTitle[] = L"Delay Lama Standalone";

// Idle animation from monksynth's MonkView: hold frame 5 with two blinks on
// frame 2, then loop a 24-step mouth shuffle until the next note.
constexpr ULONGLONG kIdleTickMs = 100;
constexpr ULONGLONG kIdleHoldTicks = 48;
constexpr int kIdleHoldFrame = 5;
constexpr int kIdleBlinkFrame = 2;
constexpr int kIdleShuffle[] = {5, 3, 4, 3, 2, 1, 0, 1, 5, 3, 4, 3,
                                5, 1, 0, 1, 2, 3, 4, 3, 5, 1, 0, 1};
constexpr ULONGLONG kIdleShuffleTicksPerStep = 2;

std::unique_ptr<Gdiplus::Bitmap> load_bitmap(const std::wstring &path) {
    if (!file_exists(path))
        return nullptr;
    auto bitmap = std::make_unique<Gdiplus::Bitmap>(path.c_str(), FALSE);
    if (bitmap->GetLastStatus() != Gdiplus::Ok)
        return nullptr;
    return bitmap;
}

int note_for_key(UINT virtual_key) {
    switch (virtual_key) {
    case 'A': return 48;
    case 'W': return 49;
    case 'S': return 50;
    case 'E': return 51;
    case 'D': return 52;
    case 'F': return 53;
    case 'T': return 54;
    case 'G': return 55;
    case 'Y': return 56;
    case 'H': return 57;
    case 'U': return 58;
    case 'J': return 59;
    case 'K': return 60;
    case 'O': return 61;
    case 'L': return 62;
    default: return -1;
    }
}

void enable_per_monitor_dpi_awareness() {
    using SetContextFn = BOOL(WINAPI *)(DPI_AWARENESS_CONTEXT);
    const auto set_context =
        reinterpret_cast<SetContextFn>(user32_proc("SetProcessDpiAwarenessContext"));
    if (!set_context || !set_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        SetProcessDPIAware();
}

} // namespace

// ---------------------------------------------------------------------------
// AppWindow

void AppWindow::destroy() {
    if (hwnd_)
        DestroyWindow(hwnd_);
}

bool AppWindow::create_window(const wchar_t *class_name, const wchar_t *title, DWORD style,
                              DWORD ex_style, HBRUSH background) {
    WNDCLASSEXW window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = app_.instance();
    window_class.lpfnWndProc = &AppWindow::window_proc;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    // The small (title bar) icon is picked from the same resource.
    window_class.hIcon = LoadIconW(app_.instance(), MAKEINTRESOURCEW(IDI_APP_ICON));
    window_class.hbrBackground = background;
    window_class.lpszClassName = class_name;
    if (!RegisterClassExW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;

    hwnd_ = CreateWindowExW(ex_style, class_name, title, style, CW_USEDEFAULT, CW_USEDEFAULT,
                            320, 240, nullptr, nullptr, app_.instance(), this);
    return hwnd_ != nullptr;
}

void AppWindow::add_system_menu_items() {
    HMENU menu = GetSystemMenu(hwnd_, FALSE);
    if (!menu)
        return;
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kSysCommandSettings, L"Settings...");
    AppendMenuW(menu, MF_STRING, kSysCommandAbout, L"About Delay Lama...");
}

LRESULT CALLBACK AppWindow::window_proc(HWND hwnd, UINT message, WPARAM w_param,
                                        LPARAM l_param) {
    AppWindow *window = reinterpret_cast<AppWindow *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW *create = reinterpret_cast<const CREATESTRUCTW *>(l_param);
        window = static_cast<AppWindow *>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
        window->hwnd_ = hwnd;
    }
    if (!window)
        return DefWindowProcW(hwnd, message, w_param, l_param);

    const LRESULT result = window->handle_message(message, w_param, l_param);
    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        window->hwnd_ = nullptr;
    }
    return result;
}

bool AppWindow::handle_common_message(UINT message, WPARAM w_param, LPARAM l_param,
                                      LRESULT &result) {
    result = 0;
    switch (message) {
    case WM_CLOSE:
        app_.close_window(id_);
        return true;
    case WM_MOVE:
    case WM_SIZE:
        app_.window_geometry_changed(id_);
        return false;
    case WM_KEYDOWN:
        if (w_param == VK_F1)
            app_.show_about(hwnd_);
        else
            app_.key_down(static_cast<UINT>(w_param), l_param);
        return true;
    case WM_KEYUP:
        app_.key_up(static_cast<UINT>(w_param));
        return true;
    case WM_KILLFOCUS:
        // Key-up events go to the newly focused window, so release held keys.
        // Touches keep their implicit capture and still end normally.
        app_.release_keys();
        return true;
    case WM_CONTEXTMENU: {
        POINT point = {GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
        if (point.x == -1 && point.y == -1) {
            RECT window_rect = {};
            GetWindowRect(hwnd_, &window_rect);
            point = {(window_rect.left + window_rect.right) / 2,
                     (window_rect.top + window_rect.bottom) / 2};
        } else if (app_.settings().window(id_).title_bar) {
            // Right-clicking the title bar keeps the normal window menu.
            POINT client_point = point;
            ScreenToClient(hwnd_, &client_point);
            RECT client = {};
            GetClientRect(hwnd_, &client);
            if (!PtInRect(&client, client_point))
                return false;
        }
        app_.show_context_menu(id_, point);
        return true;
    }
    case WM_SYSCOMMAND:
        if ((w_param & 0xFFF0) == kSysCommandSettings) {
            app_.show_window(WindowId::Settings, true, true);
            return true;
        }
        if ((w_param & 0xFFF0) == kSysCommandAbout) {
            app_.show_about(hwnd_);
            return true;
        }
        return false;
    case WM_DPICHANGED:
        // Keep the artwork's physical size when crossing monitors.
        return true;
    case WM_ENDSESSION:
        if (w_param)
            app_.save();
        return true;
    default:
        return false;
    }
}

// ---------------------------------------------------------------------------
// App

App::App(DelayLamaHandle *synth)
    : synth_(synth), audio_(synth), flag_window_(std::make_unique<FlagWindow>(*this)),
      monk_window_(std::make_unique<MonkWindow>(*this)),
      settings_window_(std::make_unique<SettingsWindow>(*this)),
      classic_window_(std::make_unique<ClassicWindow>(*this)) {}

App::~App() {
    // Destroy windows while every window object is still fully alive, then
    // stop audio before the synth handle goes away.
    quitting_ = true;
    settings_window_->destroy();
    classic_window_->destroy();
    monk_window_->destroy();
    flag_window_->destroy();
    audio_.stop();
}

bool App::load_resources() {
    resource_dir_ = module_directory() + L"\\Resources";

    auto background = load_bitmap(resource_dir_ + L"\\background.png");
    if (!background)
        return false;
    background_image_ = copy_region(*background, 0, 0, kCanvasWidth, kCanvasHeight);
    if (!background_image_)
        return false;

    auto faces = load_bitmap(resource_dir_ + L"\\faces.png");
    if (!faces)
        return false;
    for (int frame = 0; frame < kFaceFrames; ++frame) {
        const int column = frame / kFaceRowsPerColumn;
        const int row = frame % kFaceRowsPerColumn;
        auto bitmap = copy_region(*faces, column * kFaceCellWidth, row * kFaceCellHeight,
                                  kFaceWidth, kFaceHeight);
        if (!bitmap)
            return false;
        faces_.push_back(std::move(bitmap));
    }

    // A higher-resolution Resources\flag.png replaces the 164x61 original.
    if (auto custom = load_bitmap(resource_dir_ + L"\\flag.png")) {
        flag_image_ = copy_region(*custom, 0, 0, static_cast<int>(custom->GetWidth()),
                                  static_cast<int>(custom->GetHeight()));
    }
    if (!flag_image_) {
        flag_image_ = copy_region(*background_image_, kFlagSourceX, kFlagSourceY, kFlagWidth,
                                  kFlagHeight);
    }
    return flag_image_ != nullptr;
}

bool App::create_windows(HINSTANCE instance) {
    instance_ = instance;
    load_settings(settings_);
    ensure_on_screen();

    if (!flag_window_->create() || !monk_window_->create() || !settings_window_->create() ||
        !classic_window_->create())
        return false;

    apply_sound();
    apply_flag();
    for (int index = 0; index < kWindowCount; ++index)
        apply_window(static_cast<WindowId>(index));

    // The settings window always opens. The play windows of the display keep
    // their saved state unless none of them was left open.
    settings_.window(WindowId::Settings).visible = true;
    if (!any_play_window_visible()) {
        for (const WindowId id : kPlayWindows) {
            if (in_layout(id))
                settings_.window(id).visible = true;
        }
    }

    ready_ = true;
    idle_start_ms_ = GetTickCount64();
    for (int index = 0; index < kWindowCount; ++index) {
        const WindowId id = static_cast<WindowId>(index);
        if (in_layout(id) && settings_.window(id).visible)
            ShowWindow(window(id)->hwnd(), SW_SHOWNOACTIVATE);
    }
    WindowId focus = WindowId::Settings;
    for (const WindowId id : {WindowId::Classic, WindowId::Flag, WindowId::Monk}) {
        if (in_layout(id) && settings_.window(id).visible) {
            focus = id;
            break;
        }
    }
    SetForegroundWindow(window(focus)->hwnd());
    settings_window_->settings_changed();
    return true;
}

bool App::start_audio() { return audio_.start(); }

int App::run() {
    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        // Tab, Enter and arrow-key navigation inside the settings window.
        HWND settings = settings_window_->hwnd();
        if (settings && IsDialogMessageW(settings, &message))
            continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

Gdiplus::Bitmap *App::face_frame(int index) const {
    if (index < 0 || index >= static_cast<int>(faces_.size()))
        return nullptr;
    return faces_[static_cast<size_t>(index)].get();
}

// Singing maps the vowel across frames 6..29; otherwise the idle sequence
// runs from the moment the last note ended.
int App::active_frame() {
    const ULONGLONG now = GetTickCount64();
    if (synth_ && dl_is_active(synth_)) {
        was_singing_ = true;
        const int frame = 6 + static_cast<int>(dl_get_vowel(synth_) * 23.0f + 0.5f);
        return std::max(6, std::min(29, frame));
    }
    if (was_singing_) {
        was_singing_ = false;
        idle_start_ms_ = now;
    }

    const ULONGLONG ticks = (now - idle_start_ms_) / kIdleTickMs;
    if (ticks < kIdleHoldTicks) {
        const bool blink = (ticks >= 14 && ticks < 17) || (ticks >= 31 && ticks < 34);
        return blink ? kIdleBlinkFrame : kIdleHoldFrame;
    }
    const ULONGLONG step = (ticks - kIdleHoldTicks) / kIdleShuffleTicksPerStep;
    return kIdleShuffle[step % ARRAYSIZE(kIdleShuffle)];
}

AppWindow *App::window(WindowId id) const {
    switch (id) {
    case WindowId::Flag: return flag_window_.get();
    case WindowId::Monk: return monk_window_.get();
    case WindowId::Settings: return settings_window_.get();
    case WindowId::Classic: return classic_window_.get();
    }
    return nullptr;
}

bool App::in_layout(WindowId id) const {
    switch (id) {
    case WindowId::Flag:
    case WindowId::Monk: return !settings_.classic;
    case WindowId::Classic: return settings_.classic;
    case WindowId::Settings: return true;
    }
    return false;
}

void App::apply_sound() {
    const SoundSettings &sound = settings_.sound;
    dl_set_volume(synth_, sound.volume);
    dl_set_glide(synth_, sound.glide);
    dl_set_voice(synth_, sound.voice);
    dl_set_vibrato(synth_, sound.vibrato);
    dl_set_vibrato_rate(synth_, sound.vibrato_rate);
    dl_set_breath(synth_, sound.breath);
    dl_set_unison(synth_, sound.unison, sound.unison_detune, sound.unison_spread);
    dl_set_delay(synth_, sound.delay_mix);
    dl_set_delay_time(synth_, sound.delay_time);
    dl_set_delay_feedback(synth_, sound.delay_feedback);
    dl_set_envelope(synth_, sound.attack, sound.decay, sound.sustain, sound.release);
    // The classic panel shows glide, voice and echo.
    classic_window_->redraw();
}

void App::apply_flag() {
    const FlagSettings &flag = settings_.flag;
    dl_set_xy_range(synth_, static_cast<float>(flag.low_note), static_cast<float>(flag.high_note),
                    flag.snap ? 1 : 0);
    flag_window_->settings_changed();
    classic_window_->redraw();
}

void App::apply_scaling() {
    flag_window_->settings_changed();
    monk_window_->settings_changed();
    classic_window_->settings_changed();
}

// Positions a window so its client area lands exactly on the stored geometry,
// using the frame metrics of the monitor it is moving to.
void App::place_window(WindowId id) {
    HWND hwnd = window(id)->hwnd();
    WindowSettings &settings = settings_.window(id);
    const DWORD style = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_STYLE));
    const DWORD ex_style = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_EXSTYLE));

    RECT rect = {settings.x, settings.y, settings.x + std::max(1, settings.width),
                 settings.y + std::max(1, settings.height)};
    const UINT dpi = dpi_for_rect(rect);
    if (id == WindowId::Settings) {
        const SIZE size = SettingsWindow::client_size_for_dpi(dpi);
        rect.right = rect.left + size.cx;
        rect.bottom = rect.top + size.cy;
    }
    adjust_window_rect_for_dpi(rect, style, ex_style, dpi);
    SetWindowPos(hwnd, nullptr, rect.left, rect.top, rect.right - rect.left,
                 rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void App::apply_window(WindowId id) {
    HWND hwnd = window(id)->hwnd();
    if (!hwnd)
        return;
    const WindowSettings &settings = settings_.window(id);

    applying_window_ = true;
    if (IsIconic(hwnd) || IsZoomed(hwnd)) {
        // Back to a normal window first; the stored geometry already holds the
        // area it last covered.
        WINDOWPLACEMENT placement = {};
        placement.length = sizeof(placement);
        GetWindowPlacement(hwnd, &placement);
        placement.flags = 0;
        placement.showCmd = IsWindowVisible(hwnd) ? SW_SHOWNOACTIVATE : SW_HIDE;
        SetWindowPlacement(hwnd, &placement);
    }
    if (id != WindowId::Settings) {
        const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        const LONG_PTR frame = settings.title_bar ? WS_OVERLAPPEDWINDOW : WS_POPUP;
        const LONG_PTR wanted = (style & WS_VISIBLE) | frame;
        if (style != wanted)
            SetWindowLongPtrW(hwnd, GWL_STYLE, wanted);
    }
    place_window(id);
    SetWindowPos(hwnd, settings.topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    applying_window_ = false;

    window_geometry_changed(id);
    if (id != WindowId::Settings)
        window(id)->settings_changed();
}

void App::set_window_geometry(WindowId id, int x, int y, int width, int height) {
    WindowSettings &settings = settings_.window(id);
    settings.x = x;
    settings.y = y;
    settings.width = std::max(16, width);
    settings.height = std::max(16, height);
    apply_window(id);
}

void App::window_geometry_changed(WindowId id) {
    if (!ready_ || applying_window_ || quitting_)
        return;
    HWND hwnd = window(id)->hwnd();
    if (!hwnd || IsIconic(hwnd))
        return;
    RECT client = {};
    GetClientRect(hwnd, &client);
    POINT origin = {0, 0};
    ClientToScreen(hwnd, &origin);
    WindowSettings &settings = settings_.window(id);
    settings.x = origin.x;
    settings.y = origin.y;
    settings.width = client.right;
    settings.height = client.bottom;
    settings_window_->refresh_geometry(id);
}

// A window outside the current display only remembers the choice.
void App::show_window(WindowId id, bool show, bool activate) {
    HWND hwnd = window(id)->hwnd();
    if (!hwnd)
        return;
    settings_.window(id).visible = show;
    if (show && in_layout(id)) {
        ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : (activate ? SW_SHOW : SW_SHOWNOACTIVATE));
        if (activate)
            SetForegroundWindow(hwnd);
    } else if (!show) {
        if (id == WindowId::Flag)
            flag_window_->finish_drag();
        if (id == WindowId::Classic)
            classic_window_->finish_drag();
        ShowWindow(hwnd, SW_HIDE);
    }
    settings_window_->settings_changed();
}

bool App::any_play_window_visible() const {
    for (const WindowId id : kPlayWindows) {
        if (in_layout(id) && settings_.window(id).visible)
            return true;
    }
    return false;
}

// Closing the settings window exits. Closing a play window only hides it; when
// the last one goes, the settings window comes forward to show them again.
void App::close_window(WindowId id) {
    if (id == WindowId::Settings) {
        quit();
        return;
    }
    show_window(id, false, false);
    if (!any_play_window_visible())
        show_window(WindowId::Settings, true, true);
}

void App::finish_drags() {
    flag_window_->finish_drag();
    classic_window_->finish_drag();
}

// Shows the windows of the current display that are meant to be visible and
// hides the rest; the settings window is left alone.
void App::update_layout_visibility() {
    HWND active = GetForegroundWindow();
    bool lost_active = false;
    for (const WindowId id : kPlayWindows) {
        HWND hwnd = window(id)->hwnd();
        const bool show = in_layout(id) && settings_.window(id).visible;
        if (show && !IsWindowVisible(hwnd)) {
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        } else if (!show && IsWindowVisible(hwnd)) {
            lost_active = lost_active || hwnd == active;
            ShowWindow(hwnd, SW_HIDE);
        }
    }
    // Hand the focus to the new display when the focused window disappeared.
    if (lost_active) {
        const WindowId focus = settings_.classic                              ? WindowId::Classic
                               : settings_.window(WindowId::Flag).visible ? WindowId::Flag
                                                                          : WindowId::Monk;
        SetForegroundWindow(window(focus)->hwnd());
    }
}

// Switching to the classic display shows the classic window; switching to
// separate windows shows all of them (flag, monk and settings) where they were.
void App::set_classic(bool classic) {
    if (settings_.classic == classic)
        return;
    finish_drags();
    settings_.classic = classic;
    if (classic) {
        settings_.window(WindowId::Classic).visible = true;
    } else {
        settings_.window(WindowId::Flag).visible = true;
        settings_.window(WindowId::Monk).visible = true;
    }
    update_layout_visibility();
    if (!classic)
        show_window(WindowId::Settings, true, false);
    settings_window_->settings_changed();
}

void App::refresh_settings_window() { settings_window_->settings_changed(); }

void App::set_default_geometry(Settings &settings) const {
    const POINT origin = {0, 0};
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &info);
    const RECT work = info.rcWork;
    const UINT dpi = dpi_for_rect(work);

    WindowSettings &monk = settings.window(WindowId::Monk);
    monk.x = work.left + scale_px(48, dpi);
    monk.y = work.top + scale_px(56, dpi);
    monk.width = scale_px(kFaceWidth, dpi);
    monk.height = scale_px(kFaceHeight, dpi);

    WindowSettings &flag = settings.window(WindowId::Flag);
    flag.x = monk.x;
    flag.y = monk.y + monk.height + scale_px(72, dpi);
    flag.width = scale_px(kFlagWidth * 3, dpi);
    flag.height = scale_px(kFlagHeight * 3, dpi);

    // The classic panel opens at its original size where the monk would be.
    WindowSettings &classic = settings.window(WindowId::Classic);
    classic.x = monk.x;
    classic.y = monk.y;
    classic.width = scale_px(kCanvasWidth, dpi);
    classic.height = scale_px(kCanvasHeight, dpi);

    WindowSettings &panel = settings.window(WindowId::Settings);
    const SIZE panel_size = SettingsWindow::client_size_for_dpi(dpi);
    panel.x = monk.x + std::max({monk.width, flag.width, classic.width}) + scale_px(56, dpi);
    panel.y = monk.y;
    panel.width = panel_size.cx;
    panel.height = panel_size.cy;
}

// Falls back to the default spot for any window that would open mostly off
// screen (for example after a monitor was unplugged).
void App::ensure_on_screen() {
    Settings defaults = default_settings();
    set_default_geometry(defaults);
    for (int index = 0; index < kWindowCount; ++index) {
        WindowSettings &window = settings_.windows[static_cast<size_t>(index)];
        const WindowSettings &fallback = defaults.windows[static_cast<size_t>(index)];
        bool usable = window.width > 0 && window.height > 0;
        if (usable) {
            const RECT rect = {window.x, window.y, window.x + window.width,
                               window.y + window.height};
            MONITORINFO info = {};
            info.cbSize = sizeof(info);
            HMONITOR monitor = MonitorFromRect(&rect, MONITOR_DEFAULTTONULL);
            RECT visible = {};
            usable = monitor && GetMonitorInfoW(monitor, &info) &&
                     IntersectRect(&visible, &rect, &info.rcWork) &&
                     visible.right - visible.left >= std::min(window.width, 64) &&
                     visible.bottom - visible.top >= std::min(window.height, 32);
        }
        if (!usable) {
            window.x = fallback.x;
            window.y = fallback.y;
            window.width = fallback.width;
            window.height = fallback.height;
        }
    }
}

void App::reset_sound() {
    settings_.sound = SoundSettings{};
    apply_sound();
    settings_window_->settings_changed();
}

void App::reset_layout() {
    Settings defaults = default_settings();
    set_default_geometry(defaults);
    for (int index = 0; index < kWindowCount; ++index) {
        const WindowId id = static_cast<WindowId>(index);
        settings_.window(id) = defaults.window(id);
        apply_window(id);
        show_window(id, true, false);
    }
    settings_window_->settings_changed();
}

// Back to a fresh install: the classic display with every sound, flag and
// window option at its default. The settings window stays open.
void App::reset_all(HWND owner) {
    if (MessageBoxW(owner,
                    L"Reset every setting to its default? This restores the sound, the flag "
                    L"options and the classic display, with every window at its default place.",
                    kAppTitle, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
        return;
    finish_drags();
    Settings defaults = default_settings();
    set_default_geometry(defaults);
    settings_ = defaults;
    apply_sound();
    apply_flag();
    apply_scaling();
    for (int index = 0; index < kWindowCount; ++index)
        apply_window(static_cast<WindowId>(index));
    update_layout_visibility();
    show_window(WindowId::Settings, true, false);
    settings_window_->settings_changed();
}

void App::show_about(HWND owner) {
    std::wstring message =
        L"Touch or drag across the Tibetan flag to sing: horizontal controls pitch and "
        L"vertical controls the vowel.\n\n"
        L"The flag, monk and settings are separate windows. Move and resize them freely, or "
        L"set exact positions on the Windows tab of the settings. The classic display puts the "
        L"original panel, with its glide and voice knobs and echo slider, in one resizable "
        L"window instead. Right-click any window for quick options.\n\n"
        L"The settings window always opens with Delay Lama, and closing it exits. Closing any "
        L"other window only hides it; tick Show on the Windows tab to bring it back.\n\n"
        L"Computer keyboard: A W S E D F T G Y H U J K O L\n\n"
        L"MIDI: notes, pitch wheel -> vowel, CC1 vibrato, CC5 glide, CC7 volume, "
        L"CC12 delay and CC13 voice. Connected MIDI inputs: ";
    message += std::to_wstring(dl_midi_source_count(synth_));
    message += L".\n\nOpen the original Delay Lama manual?";

    const int result =
        MessageBoxW(owner, message.c_str(), kAppTitle, MB_YESNO | MB_ICONINFORMATION);
    if (result == IDYES) {
        const std::wstring manual = resource_dir_ + L"\\DelayLama-Original-Manual.pdf";
        if (file_exists(manual))
            ShellExecuteW(owner, L"open", manual.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

void App::show_context_menu(WindowId id, POINT screen_point) {
    HWND owner = window(id)->hwnd();
    HMENU menu = CreatePopupMenu();
    if (!owner || !menu)
        return;
    const WindowSettings &settings = settings_.window(id);
    auto checked = [](bool on) { return MF_STRING | (on ? MF_CHECKED : MF_UNCHECKED); };
    AppendMenuW(menu, MF_STRING, kMenuSettings, L"Settings...");
    AppendMenuW(menu, MF_STRING, kMenuAbout, L"About / manual...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, checked(settings_.classic), kMenuClassic, L"Classic display (one window)");
    if (!settings_.classic) {
        AppendMenuW(menu, checked(settings_.window(WindowId::Flag).visible), kMenuShowFlag,
                    L"Show flag window");
        AppendMenuW(menu, checked(settings_.window(WindowId::Monk).visible), kMenuShowMonk,
                    L"Show monk window");
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, checked(settings.title_bar), kMenuTitleBar, L"Title bar");
    AppendMenuW(menu, checked(settings.topmost), kMenuTopmost, L"Always on top");
    AppendMenuW(menu, checked(settings.keep_aspect), kMenuKeepAspect, L"Keep aspect ratio");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit");

    SetForegroundWindow(owner);
    const UINT command = static_cast<UINT>(TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen_point.x, screen_point.y, 0, owner, nullptr));
    DestroyMenu(menu);

    WindowSettings &target = settings_.window(id);
    switch (command) {
    case kMenuSettings:
        show_window(WindowId::Settings, true, true);
        break;
    case kMenuAbout:
        show_about(owner);
        break;
    case kMenuClassic:
        set_classic(!settings_.classic);
        break;
    case kMenuShowFlag:
        if (settings_.window(WindowId::Flag).visible)
            close_window(WindowId::Flag);
        else
            show_window(WindowId::Flag, true, true);
        break;
    case kMenuShowMonk:
        if (settings_.window(WindowId::Monk).visible)
            close_window(WindowId::Monk);
        else
            show_window(WindowId::Monk, true, true);
        break;
    case kMenuTitleBar:
        target.title_bar = !target.title_bar;
        apply_window(id);
        break;
    case kMenuTopmost:
        target.topmost = !target.topmost;
        apply_window(id);
        break;
    case kMenuKeepAspect:
        target.keep_aspect = !target.keep_aspect;
        apply_window(id);
        break;
    case kMenuExit:
        quit();
        return;
    default:
        return;
    }
    settings_window_->settings_changed();
}

void App::key_down(UINT virtual_key, LPARAM l_param) {
    if (virtual_key >= ARRAYSIZE(key_pressed_))
        return;
    const int note = note_for_key(virtual_key);
    if (note < 0)
        return;
    if ((l_param & (1LL << 30)) != 0 || key_pressed_[virtual_key])
        return;
    key_pressed_[virtual_key] = true;
    key_notes_[virtual_key] = static_cast<uint8_t>(note);
    dl_note_on(synth_, static_cast<uint8_t>(note), 1.0f);
}

void App::key_up(UINT virtual_key) {
    if (virtual_key >= ARRAYSIZE(key_pressed_) || !key_pressed_[virtual_key])
        return;
    key_pressed_[virtual_key] = false;
    dl_note_off(synth_, key_notes_[virtual_key]);
}

void App::release_keys() {
    for (UINT key = 0; key < ARRAYSIZE(key_pressed_); ++key) {
        if (key_pressed_[key])
            dl_note_off(synth_, key_notes_[key]);
        key_pressed_[key] = false;
    }
}

void App::save() { save_settings(settings_); }

void App::quit() {
    if (quitting_)
        return;
    save();
    quitting_ = true;
    finish_drags();
    release_keys();
    dl_all_notes_off(synth_);
    settings_window_->destroy();
    classic_window_->destroy();
    monk_window_->destroy();
    flag_window_->destroy();
    PostQuitMessage(0);
}

} // namespace delay_lama

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    delay_lama::enable_per_monitor_dpi_awareness();

    INITCOMMONCONTROLSEX controls = {};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_BAR_CLASSES | ICC_TAB_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    Gdiplus::GdiplusStartupInput gdiplus_input;
    ULONG_PTR gdiplus_token = 0;
    if (Gdiplus::GdiplusStartup(&gdiplus_token, &gdiplus_input, nullptr) != Gdiplus::Ok) {
        MessageBoxW(nullptr, L"Could not initialise Windows graphics.",
                    delay_lama::kAppTitle, MB_OK | MB_ICONERROR);
        return 1;
    }

    DelayLamaHandle *handle = dl_create(48000.0f);
    if (!handle) {
        MessageBoxW(nullptr, L"Could not initialise the vocal synthesis engine.",
                    delay_lama::kAppTitle, MB_OK | MB_ICONERROR);
        Gdiplus::GdiplusShutdown(gdiplus_token);
        return 1;
    }

    int exit_code = 0;
    {
        delay_lama::App app(handle);
        if (!app.load_resources()) {
            MessageBoxW(nullptr,
                        L"The Windows build could not find background.png and faces.png in its "
                        L"Resources folder.",
                        delay_lama::kAppTitle, MB_OK | MB_ICONERROR);
            exit_code = 1;
        } else if (!app.create_windows(instance)) {
            MessageBoxW(nullptr, L"Could not create the Delay Lama windows.",
                        delay_lama::kAppTitle, MB_OK | MB_ICONERROR);
            exit_code = 1;
        } else if (!app.start_audio()) {
            MessageBoxW(nullptr,
                        L"Could not open the default Windows audio device. Check that an output "
                        L"device is available and try again.",
                        delay_lama::kAppTitle, MB_OK | MB_ICONERROR);
            exit_code = 1;
        } else {
            exit_code = app.run();
        }
    }

    dl_destroy(handle);
    Gdiplus::GdiplusShutdown(gdiplus_token);
    return exit_code;
}
