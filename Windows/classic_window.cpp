#include "app.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace delay_lama {
namespace {

// Controls in background (canvas) pixels. The knob caps, the dark notched
// part, are baked into the background pointing at their minimum; drawing
// that part turned shows the value. Each rim has a light glint that must not
// turn with it. Positions were measured on the artwork.
struct Knob {
    float x;
    float y;
    int source_x;
    int source_y;
    RECT glint;
};
constexpr Knob kKnobs[2] = {{44.5f, 471.0f, 22, 449, {58, 459, 64, 465}},
                            {316.5f, 470.0f, 294, 448, {324, 453, 331, 461}}};
constexpr int kKnobSourceSize = 45;
// Glint pixels are brighter than the silver ring around the cap (178..197);
// on the turning cap they take the grey of the cap face instead.
constexpr int kGlintLuminance = 205;
constexpr int kCapLuminance = 100;
constexpr float kKnobCapRadius = 21.5f;
constexpr float kKnobHitRadius = 27.0f;
constexpr float kKnobSweepDegrees = 280.0f;
// The whole range per 100 canvas pixels of vertical drag, as in the original.
constexpr float kKnobDragRange = 100.0f;
// The echo handle slides along the groove under the flag.
constexpr float kEchoLeft = 121.0f;
constexpr float kEchoRight = 241.0f;
constexpr float kEchoY = 486.0f;
constexpr RECT kEchoHit = {94, 469, 266, 506};
constexpr RECT kHelpHit = {284, 298, 327, 335};
constexpr RECT kFlagHit = {kFlagSourceX, kFlagSourceY, kFlagSourceX + kFlagWidth,
                           kFlagSourceY + kFlagHeight};

bool contains(const RECT &rect, const Gdiplus::PointF &point) {
    return point.X >= static_cast<float>(rect.left) && point.X < static_cast<float>(rect.right) &&
           point.Y >= static_cast<float>(rect.top) && point.Y < static_cast<float>(rect.bottom);
}

// A canvas rectangle in device pixels, rounded the same way for every part so
// the monk frame lands exactly on the scaled background.
RECT to_device(const RECT &area, int x, int y, int width, int height) {
    const int area_width = area.right - area.left;
    const int area_height = area.bottom - area.top;
    return {area.left + MulDiv(x, area_width, kCanvasWidth),
            area.top + MulDiv(y, area_height, kCanvasHeight),
            area.left + MulDiv(x + width, area_width, kCanvasWidth),
            area.top + MulDiv(y + height, area_height, kCanvasHeight)};
}

} // namespace

ClassicWindow::~ClassicWindow() { clear_cache(); }

bool ClassicWindow::create() {
    if (!prepare_knobs() || !create_window(L"DelayLamaClassicWindow", L"Delay Lama Standalone",
                                           WS_OVERLAPPEDWINDOW, 0, nullptr))
        return false;
    face_cache_.assign(kFaceFrames, nullptr);
    add_system_menu_items();
    SetTimer(hwnd_, kAnimationTimer, kAnimationIntervalMs, nullptr);
    return true;
}

// Splits each knob's piece of the background into the cap, with the glint
// dimmed to the cap's grey, and a glint-only overlay. Drawn unturned, the two
// give back the original pixels.
bool ClassicWindow::prepare_knobs() {
    for (int index = 0; index < 2; ++index) {
        const Knob &knob = kKnobs[index];
        knob_caps_[index] = copy_region(*app_.background_image(), knob.source_x, knob.source_y,
                                        kKnobSourceSize, kKnobSourceSize);
        knob_glints_[index] = std::make_unique<Gdiplus::Bitmap>(kKnobSourceSize, kKnobSourceSize,
                                                                PixelFormat32bppARGB);
        Gdiplus::Bitmap *cap = knob_caps_[index].get();
        Gdiplus::Bitmap *glint = knob_glints_[index].get();
        if (!cap || glint->GetLastStatus() != Gdiplus::Ok)
            return false;
        {
            Gdiplus::Graphics clear(glint);
            clear.Clear(Gdiplus::Color(0, 0, 0, 0));
        }
        for (LONG y = knob.glint.top; y < knob.glint.bottom; ++y) {
            for (LONG x = knob.glint.left; x < knob.glint.right; ++x) {
                const INT u = x - knob.source_x;
                const INT v = y - knob.source_y;
                Gdiplus::Color color;
                cap->GetPixel(u, v, &color);
                const int luminance = (color.GetR() + color.GetG() + color.GetB()) / 3;
                if (luminance < kGlintLuminance)
                    continue;
                glint->SetPixel(u, v, color);
                auto dim = [luminance](BYTE channel) {
                    return static_cast<BYTE>(channel * kCapLuminance / luminance);
                };
                cap->SetPixel(u, v,
                              Gdiplus::Color(255, dim(color.GetR()), dim(color.GetG()),
                                             dim(color.GetB())));
            }
        }
    }
    return true;
}

void ClassicWindow::settings_changed() {
    clear_cache();
    AppWindow::settings_changed();
}

LRESULT ClassicWindow::handle_message(UINT message, WPARAM w_param, LPARAM l_param) {
    LRESULT result = 0;
    if (handle_common_message(message, w_param, l_param, result))
        return result;

    switch (message) {
    case WM_PAINT:
        paint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        redraw();
        return 0;
    case WM_TIMER:
        if (w_param == kAnimationTimer && IsWindowVisible(hwnd_) &&
            app_.active_frame() != shown_frame_)
            redraw();
        return 0;
    case WM_LBUTTONDOWN:
        mouse_down(GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param));
        return 0;
    case WM_MOUSEMOVE:
        if (drag_ != Control::None && (w_param & MK_LBUTTON))
            mouse_move(GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param));
        return 0;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        finish_drag();
        return 0;
    case WM_NCHITTEST:
        if (!app_.settings().window(id_).title_bar)
            return borderless_hit_test(l_param);
        break;
    case WM_NCRBUTTONUP:
        if (!app_.settings().window(id_).title_bar) {
            app_.show_context_menu(id_, {GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)});
            return 0;
        }
        break;
    case WM_DESTROY:
        KillTimer(hwnd_, kAnimationTimer);
        finish_drag();
        buffer_.release();
        clear_cache();
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, w_param, l_param);
}

// Without a title bar the controls still play, the rest of the picture drags
// the window and its edges resize it.
LRESULT ClassicWindow::borderless_hit_test(LPARAM l_param) const {
    POINT point = {GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
    ScreenToClient(hwnd_, &point);
    if (control_at(to_canvas(point.x, point.y)) != Control::None)
        return HTCLIENT;
    RECT client = {};
    GetClientRect(hwnd_, &client);
    return move_or_resize_hit_test(client, point, scale_px(8, dpi_for_window(hwnd_)));
}

RECT ClassicWindow::canvas_area() const {
    RECT client = {};
    GetClientRect(hwnd_, &client);
    return fit_rect(client, kCanvasWidth, kCanvasHeight, app_.settings().window(id_).keep_aspect);
}

Gdiplus::PointF ClassicWindow::to_canvas(int x, int y) const {
    const RECT area = canvas_area();
    const float width = static_cast<float>(std::max(1L, area.right - area.left));
    const float height = static_cast<float>(std::max(1L, area.bottom - area.top));
    return {(static_cast<float>(x - area.left) + 0.5f) * kCanvasWidth / width,
            (static_cast<float>(y - area.top) + 0.5f) * kCanvasHeight / height};
}

ClassicWindow::Control ClassicWindow::control_at(const Gdiplus::PointF &point) const {
    if (contains(kFlagHit, point))
        return Control::Flag;
    for (int index = 0; index < 2; ++index) {
        const Knob &knob = kKnobs[index];
        if (std::hypot(point.X - knob.x, point.Y - knob.y) <= kKnobHitRadius)
            return index == 0 ? Control::Glide : Control::Voice;
    }
    if (contains(kEchoHit, point))
        return Control::Echo;
    if (contains(kHelpHit, point))
        return Control::Help;
    return Control::None;
}

void ClassicWindow::mouse_down(int x, int y) {
    SetFocus(hwnd_);
    const Gdiplus::PointF point = to_canvas(x, y);
    const Control control = control_at(point);
    const SoundSettings &sound = app_.settings().sound;
    switch (control) {
    case Control::None:
        return;
    case Control::Help:
        app_.show_about(hwnd_);
        return;
    case Control::Flag:
        update_flag(point);
        dl_xy_begin(app_.synth(), flag_x_, 1.0f - flag_y_);
        break;
    case Control::Glide:
        drag_start_value_ = sound.glide;
        break;
    case Control::Voice:
        drag_start_value_ = sound.voice;
        break;
    case Control::Echo:
        break;
    }
    drag_ = control;
    drag_start_y_ = point.Y;
    SetCapture(hwnd_);
    if (control == Control::Echo)
        mouse_move(x, y);
    redraw();
}

// Knobs follow vertical drags from where they were grabbed; the echo handle
// jumps to the pointer.
void ClassicWindow::mouse_move(int x, int y) {
    const Gdiplus::PointF point = to_canvas(x, y);
    switch (drag_) {
    case Control::Flag:
        update_flag(point);
        dl_xy_move(app_.synth(), flag_x_, 1.0f - flag_y_);
        if (app_.settings().flag.show_crosshair)
            redraw();
        break;
    case Control::Glide:
    case Control::Voice:
        set_value(drag_, clamp01(drag_start_value_ + (drag_start_y_ - point.Y) / kKnobDragRange));
        break;
    case Control::Echo:
        set_value(drag_, clamp01((point.X - kEchoLeft) / (kEchoRight - kEchoLeft)));
        break;
    case Control::None:
    case Control::Help:
        break;
    }
}

void ClassicWindow::update_flag(const Gdiplus::PointF &point) {
    flag_x_ = clamp01((point.X - static_cast<float>(kFlagSourceX)) / kFlagWidth);
    flag_y_ = clamp01((point.Y - static_cast<float>(kFlagSourceY)) / kFlagHeight);
}

void ClassicWindow::set_value(Control control, float value) {
    SoundSettings &sound = app_.settings().sound;
    switch (control) {
    case Control::Glide:
        sound.glide = value;
        dl_set_glide(app_.synth(), value);
        break;
    case Control::Voice:
        sound.voice = value;
        dl_set_voice(app_.synth(), value);
        break;
    case Control::Echo:
        sound.delay_mix = value;
        dl_set_delay(app_.synth(), value);
        break;
    case Control::None:
    case Control::Flag:
    case Control::Help:
        return;
    }
    app_.refresh_settings_window();
    redraw();
}

void ClassicWindow::finish_drag() {
    if (drag_ == Control::None)
        return;
    const bool flag = drag_ == Control::Flag;
    drag_ = Control::None;
    if (flag)
        dl_xy_end(app_.synth());
    if (GetCapture() == hwnd_)
        ReleaseCapture();
    redraw();
}

void ClassicWindow::clear_cache() {
    if (background_cache_)
        DeleteObject(background_cache_);
    background_cache_ = nullptr;
    for (HBITMAP &bitmap : face_cache_) {
        if (bitmap)
            DeleteObject(bitmap);
        bitmap = nullptr;
    }
    cache_width_ = 0;
    cache_height_ = 0;
}

void ClassicWindow::paint() {
    PAINTSTRUCT paint_struct = {};
    HDC target = BeginPaint(hwnd_, &paint_struct);
    RECT client = {};
    GetClientRect(hwnd_, &client);

    HDC buffer = buffer_.prepare(target, client.right, client.bottom);
    if (buffer) {
        FillRect(buffer, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        const RECT area = canvas_area();
        const int width = area.right - area.left;
        const int height = area.bottom - area.top;
        const bool smooth = app_.settings().smooth_scaling;
        if (width > 0 && height > 0) {
            if (width != cache_width_ || height != cache_height_) {
                clear_cache();
                cache_width_ = width;
                cache_height_ = height;
            }
            if (!background_cache_)
                background_cache_ =
                    render_scaled(target, app_.background_image(), width, height, smooth);
            if (background_cache_)
                blit_bitmap(buffer, area.left, area.top, width, height, background_cache_);
            draw_face(target, buffer, area);

            const SoundSettings &sound = app_.settings().sound;
            const FlagSettings &flag = app_.settings().flag;
            Gdiplus::Graphics graphics(buffer);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            graphics.SetInterpolationMode(smooth ? Gdiplus::InterpolationModeHighQualityBicubic
                                                 : Gdiplus::InterpolationModeNearestNeighbor);
            graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
            draw_knob(graphics, area, 0, sound.glide);
            draw_knob(graphics, area, 1, sound.voice);

            graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeDefault);
            graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
            const RECT flag_area =
                to_device(area, kFlagSourceX, kFlagSourceY, kFlagWidth, kFlagHeight);
            if (flag.show_grid)
                draw_note_grid(graphics, flag_area, flag);
            if (drag_ == Control::Flag && flag.show_crosshair)
                draw_crosshair(graphics, flag_area, flag_x_, flag_y_);

            // The echo handle is drawn in canvas pixels so it scales with the panel.
            graphics.TranslateTransform(static_cast<float>(area.left),
                                        static_cast<float>(area.top));
            graphics.ScaleTransform(static_cast<float>(width) / kCanvasWidth,
                                    static_cast<float>(height) / kCanvasHeight);
            draw_echo_handle(graphics, sound.delay_mix);
        }
        buffer_.present(target);
    }
    EndPaint(hwnd_, &paint_struct);
}

// Same frame cache as the monk window, sized to the frame's spot on the panel.
void ClassicWindow::draw_face(HDC target, HDC buffer, const RECT &area) {
    const RECT face = to_device(area, kFaceX, kFaceY, kFaceWidth, kFaceHeight);
    const int width = face.right - face.left;
    const int height = face.bottom - face.top;
    const int frame = app_.active_frame();
    shown_frame_ = frame;
    Gdiplus::Bitmap *image = app_.face_frame(frame);
    if (!image || width <= 0 || height <= 0)
        return;

    const bool smooth = app_.settings().smooth_scaling;
    const bool cacheable =
        static_cast<int64_t>(width) * height * 4 * kFaceFrames <= kFrameCacheBudgetBytes;
    HBITMAP &cached = face_cache_[static_cast<size_t>(frame)];
    if (cacheable && !cached)
        cached = render_scaled(target, image, width, height, smooth);
    if (cacheable && cached) {
        blit_bitmap(buffer, face.left, face.top, width, height, cached);
    } else {
        Gdiplus::Graphics graphics(buffer);
        draw_image_scaled(graphics, image, Gdiplus::Rect(face.left, face.top, width, height),
                          smooth);
    }
}

// Paints the knob's own cap turned clockwise by the value, then its glint.
void ClassicWindow::draw_knob(Gdiplus::Graphics &graphics, const RECT &area, int index,
                              float value) const {
    const Knob &knob = kKnobs[index];
    Gdiplus::TextureBrush cap(knob_caps_[index].get(), Gdiplus::WrapModeTileFlipXY);
    if (cap.GetLastStatus() != Gdiplus::Ok)
        return;
    const float scale_x = static_cast<float>(area.right - area.left) / kCanvasWidth;
    const float scale_y = static_cast<float>(area.bottom - area.top) / kCanvasHeight;
    const float center_x = static_cast<float>(area.left) + knob.x * scale_x;
    const float center_y = static_cast<float>(area.top) + knob.y * scale_y;
    // Texture pixels -> around the knob centre -> turned -> scaled -> placed.
    cap.TranslateTransform(static_cast<float>(knob.source_x) - knob.x,
                           static_cast<float>(knob.source_y) - knob.y, Gdiplus::MatrixOrderAppend);
    cap.RotateTransform(clamp01(value) * kKnobSweepDegrees, Gdiplus::MatrixOrderAppend);
    cap.ScaleTransform(scale_x, scale_y, Gdiplus::MatrixOrderAppend);
    cap.TranslateTransform(center_x, center_y, Gdiplus::MatrixOrderAppend);
    graphics.FillEllipse(&cap, Gdiplus::RectF(center_x - kKnobCapRadius * scale_x,
                                              center_y - kKnobCapRadius * scale_y,
                                              kKnobCapRadius * 2.0f * scale_x,
                                              kKnobCapRadius * 2.0f * scale_y));
    graphics.DrawImage(knob_glints_[index].get(),
                       Gdiplus::RectF(static_cast<float>(area.left) + knob.source_x * scale_x,
                                      static_cast<float>(area.top) + knob.source_y * scale_y,
                                      kKnobSourceSize * scale_x, kKnobSourceSize * scale_y));
}

// A small brass fader cap sitting in the groove.
void ClassicWindow::draw_echo_handle(Gdiplus::Graphics &graphics, float value) const {
    const float x = kEchoLeft + clamp01(value) * (kEchoRight - kEchoLeft);
    const Gdiplus::RectF body(x - 6.0f, kEchoY - 11.0f, 12.0f, 22.0f);
    const float diameter = 6.0f;
    Gdiplus::GraphicsPath path;
    path.AddArc(body.X, body.Y, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(body.GetRight() - diameter, body.Y, diameter, diameter, 270.0f, 90.0f);
    path.AddArc(body.GetRight() - diameter, body.GetBottom() - diameter, diameter, diameter, 0.0f,
                90.0f);
    path.AddArc(body.X, body.GetBottom() - diameter, diameter, diameter, 90.0f, 90.0f);
    path.CloseFigure();

    Gdiplus::GraphicsPath shadow_path;
    shadow_path.AddPath(&path, FALSE);
    Gdiplus::Matrix offset;
    offset.Translate(1.5f, 1.5f);
    shadow_path.Transform(&offset);
    Gdiplus::SolidBrush shadow(Gdiplus::Color(120, 40, 20, 0));
    graphics.FillPath(&shadow, &shadow_path);

    Gdiplus::LinearGradientBrush fill(body, Gdiplus::Color(255, 250, 222, 140),
                                      Gdiplus::Color(255, 150, 92, 32),
                                      Gdiplus::LinearGradientModeHorizontal);
    graphics.FillPath(&fill, &path);
    Gdiplus::Pen outline(Gdiplus::Color(255, 86, 50, 18), 1.0f);
    graphics.DrawPath(&outline, &path);
    Gdiplus::Pen mark(Gdiplus::Color(220, 86, 50, 18), 1.4f);
    graphics.DrawLine(&mark, x, body.Y + 4.0f, x, body.GetBottom() - 4.0f);
}

} // namespace delay_lama
