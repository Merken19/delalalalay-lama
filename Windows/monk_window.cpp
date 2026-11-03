#include "app.h"

#include <windowsx.h>

#include <cstdint>

namespace delay_lama {

MonkWindow::~MonkWindow() { clear_cache(); }

bool MonkWindow::create() {
    if (!create_window(L"DelayLamaMonkWindow", L"Delay Lama Monk", WS_OVERLAPPEDWINDOW, 0,
                       nullptr))
        return false;
    frame_cache_.assign(kFaceFrames, nullptr);
    add_system_menu_items();
    SetTimer(hwnd_, kAnimationTimer, kAnimationIntervalMs, nullptr);
    return true;
}

void MonkWindow::settings_changed() {
    clear_cache();
    AppWindow::settings_changed();
}

LRESULT MonkWindow::handle_message(UINT message, WPARAM w_param, LPARAM l_param) {
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
    case WM_TIMER:
        if (w_param == kAnimationTimer && app_.active_frame() != shown_frame_)
            InvalidateRect(hwnd_, nullptr, FALSE);
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
        buffer_.release();
        clear_cache();
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, w_param, l_param);
}

// Without a title bar the whole picture drags the window and its edges resize
// it; the monk is not interactive, so nothing is lost.
LRESULT MonkWindow::borderless_hit_test(LPARAM l_param) const {
    POINT point = {GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
    ScreenToClient(hwnd_, &point);
    RECT client = {};
    GetClientRect(hwnd_, &client);
    return move_or_resize_hit_test(client, point, scale_px(8, dpi_for_window(hwnd_)));
}

void MonkWindow::clear_cache() {
    for (HBITMAP &bitmap : frame_cache_) {
        if (bitmap)
            DeleteObject(bitmap);
        bitmap = nullptr;
    }
    cache_width_ = 0;
    cache_height_ = 0;
}

void MonkWindow::paint() {
    PAINTSTRUCT paint_struct = {};
    HDC target = BeginPaint(hwnd_, &paint_struct);
    RECT client = {};
    GetClientRect(hwnd_, &client);

    HDC buffer = buffer_.prepare(target, client.right, client.bottom);
    if (buffer) {
        FillRect(buffer, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        const RECT area = fit_rect(client, kFaceWidth, kFaceHeight,
                                   app_.settings().window(id_).keep_aspect);
        const int width = area.right - area.left;
        const int height = area.bottom - area.top;
        const int frame = app_.active_frame();
        shown_frame_ = frame;
        Gdiplus::Bitmap *image = app_.face_frame(frame);
        const bool smooth = app_.settings().smooth_scaling;

        if (image && width > 0 && height > 0) {
            if (width != cache_width_ || height != cache_height_) {
                clear_cache();
                cache_width_ = width;
                cache_height_ = height;
            }
            const bool cacheable =
                static_cast<int64_t>(width) * height * 4 * kFaceFrames <= kFrameCacheBudgetBytes;
            HBITMAP &cached = frame_cache_[static_cast<size_t>(frame)];
            if (cacheable && !cached)
                cached = render_scaled(target, image, width, height, smooth);
            if (cacheable && cached) {
                blit_bitmap(buffer, area.left, area.top, width, height, cached);
            } else {
                Gdiplus::Graphics graphics(buffer);
                draw_image_scaled(graphics, image,
                                  Gdiplus::Rect(area.left, area.top, width, height), smooth);
            }
        }
        buffer_.present(target);
    }
    EndPaint(hwnd_, &paint_struct);
}

} // namespace delay_lama
