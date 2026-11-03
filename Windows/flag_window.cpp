#include "app.h"

#include <windowsx.h>

#include <algorithm>

namespace delay_lama {

FlagWindow::~FlagWindow() { clear_cache(); }

bool FlagWindow::create() {
    if (!create_window(L"DelayLamaFlagWindow", L"Delay Lama Flag", WS_OVERLAPPEDWINDOW, 0,
                       nullptr))
        return false;
    add_system_menu_items();
    return true;
}

void FlagWindow::settings_changed() {
    clear_cache();
    AppWindow::settings_changed();
}

// Touchscreens reach the flag as ordinary mouse messages, so mouse, pen and
// touch all play through the same drag handling as the original window.
LRESULT FlagWindow::handle_message(UINT message, WPARAM w_param, LPARAM l_param) {
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
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        mouse_down(GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param));
        return 0;
    case WM_MOUSEMOVE:
        if (dragging_ && (w_param & MK_LBUTTON))
            mouse_move(GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param));
        return 0;
    case WM_LBUTTONUP:
        finish_drag();
        return 0;
    case WM_CAPTURECHANGED:
        finish_drag();
        return 0;
    case WM_DESTROY:
        finish_drag();
        buffer_.release();
        clear_cache();
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, w_param, l_param);
}

RECT FlagWindow::flag_area() const {
    RECT client = {};
    GetClientRect(hwnd_, &client);
    Gdiplus::Bitmap *image = app_.flag_image();
    return fit_rect(client, static_cast<int>(image->GetWidth()),
                    static_cast<int>(image->GetHeight()),
                    app_.settings().window(id_).keep_aspect);
}

void FlagWindow::update_position(int x, int y) {
    const RECT area = flag_area();
    const float width = static_cast<float>(std::max(1L, area.right - area.left));
    const float height = static_cast<float>(std::max(1L, area.bottom - area.top));
    drag_x_ = clamp01((static_cast<float>(x - area.left) + 0.5f) / width);
    drag_y_ = clamp01((static_cast<float>(y - area.top) + 0.5f) / height);
}

// Left/right is pitch; up is the brighter end of the vowel range.
void FlagWindow::mouse_down(int x, int y) {
    SetFocus(hwnd_);
    const RECT area = flag_area();
    if (x < area.left || x >= area.right || y < area.top || y >= area.bottom)
        return;
    dragging_ = true;
    update_position(x, y);
    dl_xy_begin(app_.synth(), drag_x_, 1.0f - drag_y_);
    SetCapture(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void FlagWindow::mouse_move(int x, int y) {
    update_position(x, y);
    dl_xy_move(app_.synth(), drag_x_, 1.0f - drag_y_);
    if (app_.settings().flag.show_crosshair)
        InvalidateRect(hwnd_, nullptr, FALSE);
}

void FlagWindow::finish_drag() {
    if (!dragging_)
        return;
    dragging_ = false;
    dl_xy_end(app_.synth());
    if (GetCapture() == hwnd_)
        ReleaseCapture();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void FlagWindow::clear_cache() {
    if (flag_cache_)
        DeleteObject(flag_cache_);
    flag_cache_ = nullptr;
    cache_width_ = 0;
    cache_height_ = 0;
}

void FlagWindow::paint() {
    PAINTSTRUCT paint_struct = {};
    HDC target = BeginPaint(hwnd_, &paint_struct);
    RECT client = {};
    GetClientRect(hwnd_, &client);

    HDC buffer = buffer_.prepare(target, client.right, client.bottom);
    if (buffer) {
        FillRect(buffer, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        const RECT area = flag_area();
        const int width = area.right - area.left;
        const int height = area.bottom - area.top;
        if (width > 0 && height > 0) {
            if (!flag_cache_ || width != cache_width_ || height != cache_height_) {
                clear_cache();
                flag_cache_ = render_scaled(target, app_.flag_image(), width, height,
                                            app_.settings().smooth_scaling);
                cache_width_ = width;
                cache_height_ = height;
            }
            if (flag_cache_)
                blit_bitmap(buffer, area.left, area.top, width, height, flag_cache_);

            Gdiplus::Graphics graphics(buffer);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
            if (app_.settings().flag.show_grid)
                draw_note_grid(graphics, area, app_.settings().flag);
            if (dragging_ && app_.settings().flag.show_crosshair)
                draw_crosshair(graphics, area, drag_x_, drag_y_);
        }
        buffer_.present(target);
    }
    EndPaint(hwnd_, &paint_struct);
}

} // namespace delay_lama
