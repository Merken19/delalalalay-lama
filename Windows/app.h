#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include "audio_engine.h"
#include "monk_animation.h"
#include "monk_art.h"
#include "../Sources/standalone_bridge.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace delay_lama {

enum class WindowId { Flag = 0, Monk = 1, Settings = 2, Classic = 3 };
constexpr int kWindowCount = 4;
// Every window but settings; which of them show depends on the display.
constexpr WindowId kPlayWindows[] = {WindowId::Flag, WindowId::Monk, WindowId::Classic};

// The whole flag, yellow border included, is cut out of the classic 360x510
// background (the original only reacted to 98,365-262,426 of it). Monk frames
// sit in 314x311 cells of a 5x6 sheet (column-major); each cell is a 311-px
// frame plus a 3-px border (see monksynth kFrameWidth), and its bottom 20 rows
// show the top of the nameplate, so both are cropped away. On the background
// the frames line up with the panel pixels at kFaceX, kFaceY.
constexpr int kCanvasWidth = 360;
constexpr int kCanvasHeight = 510;
constexpr int kFaceX = 22;
constexpr int kFaceY = 5;
constexpr int kFlagSourceX = 98;
constexpr int kFlagSourceY = 362;
constexpr int kFlagWidth = 163;
constexpr int kFlagHeight = 79;
constexpr int kFaceCellWidth = 314;
constexpr int kFaceCellHeight = 311;
constexpr int kFaceWidth = 311;
constexpr int kFaceHeight = 291;
constexpr int kFaceFrames = 30;
constexpr int kFaceRowsPerColumn = 6;

// Monk animation in the monk and classic windows.
constexpr UINT_PTR kAnimationTimer = 1;
constexpr UINT kAnimationIntervalMs = 30;
// Pre-scaled frames make playback a plain BitBlt; above this budget (very
// large windows) frames are scaled on the fly instead.
constexpr int64_t kFrameCacheBudgetBytes = 256ll * 1024 * 1024;

// ---------------------------------------------------------------------------
// Settings (persisted to settings.ini)

struct WindowSettings {
    bool visible = true;
    bool title_bar = true;
    bool topmost = false;
    bool keep_aspect = false;
    // Inner (client) area in physical screen pixels; width 0 means unplaced.
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct SoundSettings {
    float volume = 0.28f;
    float glide = 0.5f;
    float voice = 0.5f;
    float vibrato = 0.15f;
    float vibrato_rate = 0.5f;
    float breath = 0.5f;
    int unison = 1;
    float unison_detune = 0.0f; // cents
    float unison_spread = 0.0f;
    float delay_mix = 0.42f;
    float delay_time = 0.5f;
    float delay_feedback = 0.5f;
    float attack = 0.0f; // seconds
    float decay = 0.0f;  // seconds
    float sustain = 1.0f;
    float release = 0.0f; // seconds
};

struct FlagSettings {
    int low_note = 48;  // C3
    int high_note = 72; // C5
    bool snap = false;
    bool show_grid = false;
    bool show_crosshair = true;
};

struct Settings {
    SoundSettings sound;
    FlagSettings flag;
    // Classic: one window with the original panel instead of flag + monk.
    bool classic = true;
    bool smooth_scaling = false;
    std::array<WindowSettings, kWindowCount> windows;

    WindowSettings &window(WindowId id) { return windows[static_cast<size_t>(id)]; }
    const WindowSettings &window(WindowId id) const {
        return windows[static_cast<size_t>(id)];
    }
};

Settings default_settings();
void load_settings(Settings &settings);
void save_settings(const Settings &settings);

// ---------------------------------------------------------------------------
// Shared Win32 helpers

float clamp01(float value);
std::wstring module_directory();
bool file_exists(const std::wstring &path);
FARPROC user32_proc(const char *name);
// Per-monitor DPI awareness (v2), or system awareness on older Windows 10.
void enable_per_monitor_dpi_awareness();
UINT dpi_for_window(HWND hwnd);
UINT dpi_for_rect(const RECT &rect);
int scale_px(int value, UINT dpi);
void adjust_window_rect_for_dpi(RECT &rect, DWORD style, DWORD ex_style, UINT dpi);
RECT fit_rect(const RECT &bounds, int content_width, int content_height, bool keep_aspect);
// WM_NCHITTEST result for a borderless window: edges resize, the rest moves.
LRESULT move_or_resize_hit_test(const RECT &client, POINT point, int margin);
std::wstring note_name(int midi_note);
// Null if the file is missing or not an image.
std::unique_ptr<Gdiplus::Bitmap> load_bitmap(const std::wstring &path);
std::unique_ptr<Gdiplus::Bitmap> copy_region(Gdiplus::Image &source, int x, int y, int width,
                                             int height);
void draw_image_scaled(Gdiplus::Graphics &graphics, Gdiplus::Image *image,
                       const Gdiplus::Rect &destination, bool smooth);
HBITMAP render_scaled(HDC reference, Gdiplus::Image *image, int width, int height, bool smooth);
void blit_bitmap(HDC target, int x, int y, int width, int height, HBITMAP bitmap);
// Flag overlays, drawn in device pixels over the flag's area.
void draw_note_grid(Gdiplus::Graphics &graphics, const RECT &area, const FlagSettings &flag);
void draw_crosshair(Gdiplus::Graphics &graphics, const RECT &area, float x, float y);

// Persistent off-screen surface for flicker-free painting.
class BackBuffer {
public:
    BackBuffer() = default;
    ~BackBuffer() { release(); }
    BackBuffer(const BackBuffer &) = delete;
    BackBuffer &operator=(const BackBuffer &) = delete;

    HDC prepare(HDC reference, int width, int height);
    void present(HDC target) const;
    void release();

private:
    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previous_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};

// ---------------------------------------------------------------------------
// Windows

class App;

class AppWindow {
public:
    AppWindow(App &app, WindowId id) : app_(app), id_(id) {}
    virtual ~AppWindow() = default;
    AppWindow(const AppWindow &) = delete;
    AppWindow &operator=(const AppWindow &) = delete;

    HWND hwnd() const { return hwnd_; }
    virtual bool create() = 0;
    // Called after the settings that affect this window changed.
    virtual void settings_changed() { redraw(); }
    void redraw() const {
        if (hwnd_)
            InvalidateRect(hwnd_, nullptr, FALSE);
    }
    void destroy();

protected:
    bool create_window(const wchar_t *class_name, const wchar_t *title, DWORD style,
                       DWORD ex_style, HBRUSH background);
    void add_system_menu_items();
    virtual LRESULT handle_message(UINT message, WPARAM w_param, LPARAM l_param) = 0;
    // Closing, geometry tracking, computer-keyboard notes, menus and session
    // end handling shared by the flag and monk windows.
    bool handle_common_message(UINT message, WPARAM w_param, LPARAM l_param, LRESULT &result);

    App &app_;
    const WindowId id_;
    HWND hwnd_ = nullptr;

private:
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM w_param,
                                        LPARAM l_param);
};

class FlagWindow final : public AppWindow {
public:
    explicit FlagWindow(App &app) : AppWindow(app, WindowId::Flag) {}
    ~FlagWindow() override;

    bool create() override;
    void settings_changed() override;
    void finish_drag();

private:
    LRESULT handle_message(UINT message, WPARAM w_param, LPARAM l_param) override;
    RECT flag_area() const;
    void update_position(int x, int y);
    void mouse_down(int x, int y);
    void mouse_move(int x, int y);
    void paint();
    void clear_cache();

    // Drag position normalized to the flag image.
    bool dragging_ = false;
    float drag_x_ = 0.5f;
    float drag_y_ = 0.5f;
    BackBuffer buffer_;
    HBITMAP flag_cache_ = nullptr;
    int cache_width_ = 0;
    int cache_height_ = 0;
};

class MonkWindow final : public AppWindow {
public:
    explicit MonkWindow(App &app) : AppWindow(app, WindowId::Monk) {}
    ~MonkWindow() override;

    bool create() override;
    void settings_changed() override;

private:
    LRESULT handle_message(UINT message, WPARAM w_param, LPARAM l_param) override;
    LRESULT borderless_hit_test(LPARAM l_param) const;
    void paint();

    BackBuffer buffer_;
    FaceCache faces_;
    int shown_frame_ = -1;
};

// The original single-window panel (monk, flag, glide and voice knobs, echo
// slider, help button) scaled to any window size.
class ClassicWindow final : public AppWindow {
public:
    explicit ClassicWindow(App &app) : AppWindow(app, WindowId::Classic) {}
    ~ClassicWindow() override;

    bool create() override;
    void settings_changed() override;
    void finish_drag();

private:
    enum class Control { None, Flag, Glide, Voice, Echo, Help };

    LRESULT handle_message(UINT message, WPARAM w_param, LPARAM l_param) override;
    LRESULT borderless_hit_test(LPARAM l_param) const;
    RECT canvas_area() const;
    Gdiplus::PointF to_canvas(int x, int y) const;
    Control control_at(const Gdiplus::PointF &point) const;
    void mouse_down(int x, int y);
    void mouse_move(int x, int y);
    void update_flag(const Gdiplus::PointF &point);
    void set_value(Control control, float value);
    bool prepare_knobs();
    void paint();
    void draw_face(HDC target, HDC buffer, const RECT &area);
    void draw_knob(Gdiplus::Graphics &graphics, const RECT &area, int index, float value) const;
    void draw_echo_handle(Gdiplus::Graphics &graphics, float value) const;
    void clear_cache();

    // Per knob: the cap that turns, and the rim glint that stays put.
    std::unique_ptr<Gdiplus::Bitmap> knob_caps_[2];
    std::unique_ptr<Gdiplus::Bitmap> knob_glints_[2];
    Control drag_ = Control::None;
    float drag_start_y_ = 0.0f;
    float drag_start_value_ = 0.0f;
    // Flag drag position normalized to the flag image.
    float flag_x_ = 0.5f;
    float flag_y_ = 0.5f;
    BackBuffer buffer_;
    HBITMAP background_cache_ = nullptr;
    std::vector<HBITMAP> face_cache_;
    int cache_width_ = 0;
    int cache_height_ = 0;
    int shown_frame_ = -1;
};

class SettingsWindow final : public AppWindow {
public:
    explicit SettingsWindow(App &app) : AppWindow(app, WindowId::Settings) {}
    ~SettingsWindow() override;

    bool create() override;
    // Re-reads every control from the current settings.
    void settings_changed() override;
    void refresh_geometry(WindowId id);
    static SIZE client_size_for_dpi(UINT dpi);

private:
    struct Control {
        HWND hwnd;
        int page;   // -1: every page
        int layout; // -1: both, 0: separate windows only, 1: classic only
        RECT design;
        bool heading;
    };
    struct Slider {
        HWND slider;
        HWND value;
        std::function<int()> get;
        std::function<void(int)> set;
        std::function<std::wstring(int)> format;
    };
    struct Check {
        HWND hwnd;
        std::function<bool()> get;
        std::function<void(bool)> set;
    };
    struct GeometryGroup {
        WindowId id;
        std::array<HWND, 4> edits;
        HWND apply;
    };

    LRESULT handle_message(UINT message, WPARAM w_param, LPARAM l_param) override;
    HWND add(int page, const wchar_t *class_name, const wchar_t *text, DWORD style, int x,
             int y, int width, int height, bool heading = false);
    void add_slider(int page, int row, const wchar_t *label, int minimum, int maximum,
                    std::function<int()> get, std::function<void(int)> set,
                    std::function<std::wstring(int)> format);
    void add_check(int page, int x, int y, int width, const wchar_t *label,
                   std::function<bool()> get, std::function<void(bool)> set,
                   DWORD style = WS_TABSTOP | BS_AUTOCHECKBOX);
    void add_window_group(int y, const wchar_t *heading, WindowId id);
    void build_controls();
    void layout(UINT dpi);
    void update_fonts(UINT dpi);
    void show_page(int page);
    void apply_geometry(const GeometryGroup &group);
    void on_command(HWND control, UINT code, UINT id);
    void on_scroll(HWND control);

    HWND tab_ = nullptr;
    HFONT font_ = nullptr;
    HFONT heading_font_ = nullptr;
    int page_ = 0;
    bool shown_classic_ = false;
    // Layout tag for the controls add() creates next.
    int adding_layout_ = -1;
    std::vector<Control> controls_;
    std::vector<Slider> sliders_;
    std::vector<Check> checks_;
    std::vector<GeometryGroup> geometry_;
    std::vector<std::pair<HWND, std::function<void()>>> buttons_;
};

// ---------------------------------------------------------------------------
// Application: owns the synth-facing state shared by every window.

class App {
public:
    explicit App(DelayLamaHandle *synth);
    ~App();
    App(const App &) = delete;
    App &operator=(const App &) = delete;

    bool load_resources();
    bool create_windows(HINSTANCE instance);
    bool start_audio();
    int run();

    HINSTANCE instance() const { return instance_; }
    DelayLamaHandle *synth() const { return synth_; }
    Settings &settings() { return settings_; }
    Gdiplus::Bitmap *flag_image() const { return flag_image_.get(); }
    Gdiplus::Bitmap *background_image() const { return background_image_.get(); }
    Gdiplus::Bitmap *face_frame(int index) const;
    int active_frame();
    // Whether a window belongs to the current display (flag + monk, or classic).
    bool in_layout(WindowId id) const;

    void apply_sound();
    void apply_flag();
    void apply_scaling();
    void apply_window(WindowId id);
    void set_window_geometry(WindowId id, int x, int y, int width, int height);
    void window_geometry_changed(WindowId id);
    void show_window(WindowId id, bool show, bool activate);
    void close_window(WindowId id);
    void set_classic(bool classic);
    void refresh_settings_window();
    void reset_sound();
    void reset_layout();
    void reset_all(HWND owner);
    void show_about(HWND owner);
    void show_context_menu(WindowId id, POINT screen_point);
    void key_down(UINT virtual_key, LPARAM l_param);
    void key_up(UINT virtual_key);
    void release_keys();
    void save();
    void quit();

private:
    AppWindow *window(WindowId id) const;
    void place_window(WindowId id);
    void set_default_geometry(Settings &settings) const;
    void ensure_on_screen();
    bool any_play_window_visible() const;
    void finish_drags();
    void update_layout_visibility();

    HINSTANCE instance_ = nullptr;
    DelayLamaHandle *synth_ = nullptr;
    AudioEngine audio_;
    Settings settings_;
    std::wstring resource_dir_;
    std::unique_ptr<Gdiplus::Bitmap> background_image_;
    std::unique_ptr<Gdiplus::Bitmap> flag_image_;
    FaceFrames faces_;
    std::unique_ptr<FlagWindow> flag_window_;
    std::unique_ptr<MonkWindow> monk_window_;
    std::unique_ptr<SettingsWindow> settings_window_;
    std::unique_ptr<ClassicWindow> classic_window_;
    MonkAnimator monk_animation_;
    bool ready_ = false;
    bool applying_window_ = false;
    bool quitting_ = false;
    bool key_pressed_[256] = {};
    uint8_t key_notes_[256] = {};
};

} // namespace delay_lama
