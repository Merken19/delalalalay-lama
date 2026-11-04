#include "voice_window.h"

#include "resource.h"

#include <windowsx.h>

namespace delay_lama::voice {
namespace {

const wchar_t kWindowClass[] = L"DelayLamaVoiceWindow";
// Default size of one monk, in pixels at 96 DPI.
constexpr int kDefaultMonkHeight = 160;

} // namespace

ChoirWindow::ChoirWindow(const Choir &choir, const FaceFrames &faces, WindowOptions options)
    : choir_(choir), faces_(faces), options_(std::move(options)) {
    for (int monk = 0; monk < choir_.monks(); ++monk)
        animators_.emplace_back(MonkAnimator::choir_idle_offset(monk));
    frames_.assign(animators_.size(), -1);
}

ChoirWindow::~ChoirWindow() {
    if (hwnd_)
        DestroyWindow(hwnd_);
}

bool ChoirWindow::create(HINSTANCE instance) {
    WNDCLASSEXW window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = instance;
    window_class.lpfnWndProc = &ChoirWindow::window_proc;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    window_class.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;

    const DWORD style = options_.title_bar ? WS_OVERLAPPEDWINDOW : WS_POPUP;
    const DWORD ex_style = options_.topmost ? WS_EX_TOPMOST : 0;
    RECT rect = {options_.x, options_.y, options_.x + 1, options_.y + 1};
    const UINT dpi = options_.has_position ? dpi_for_rect(rect) : dpi_for_window(nullptr);
    const int monk_height = scale_px(kDefaultMonkHeight, dpi);
    const int width = options_.width > 0
                          ? options_.width
                          : choir_.monks() * MulDiv(monk_height, kFaceWidth, kFaceHeight);
    const int height = options_.height > 0 ? options_.height : monk_height;
    rect.right = rect.left + width;
    rect.bottom = rect.top + height;
    adjust_window_rect_for_dpi(rect, style, ex_style, dpi);

    const int x = options_.has_position ? rect.left : CW_USEDEFAULT;
    const int y = options_.has_position ? rect.top : CW_USEDEFAULT;
    hwnd_ = CreateWindowExW(ex_style, kWindowClass, options_.title.c_str(), style, x, y,
                            rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr,
                            instance, this);
    if (!hwnd_)
        return false;

    update_frames();
    SetTimer(hwnd_, kAnimationTimer, kAnimationIntervalMs, nullptr);
    // Monks appear during playback; they must not take the focus.
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd_);
    return true;
}

LRESULT CALLBACK ChoirWindow::window_proc(HWND hwnd, UINT message, WPARAM w_param,
                                          LPARAM l_param) {
    auto *window = reinterpret_cast<ChoirWindow *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto *create = reinterpret_cast<const CREATESTRUCTW *>(l_param);
        window = static_cast<ChoirWindow *>(create->lpCreateParams);
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

LRESULT ChoirWindow::handle_message(UINT message, WPARAM w_param, LPARAM l_param) {
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
        if (w_param == kAnimationTimer && update_frames())
            InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_NCHITTEST:
        if (!options_.title_bar) {
            POINT point = {GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
            ScreenToClient(hwnd_, &point);
            RECT client = {};
            GetClientRect(hwnd_, &client);
            return move_or_resize_hit_test(client, point, scale_px(8, dpi_for_window(hwnd_)));
        }
        break;
    case WM_DPICHANGED:
        // Keep the artwork's physical size when crossing monitors.
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd_, kAnimationTimer);
        buffer_.release();
        cache_.clear();
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, w_param, l_param);
}

bool ChoirWindow::update_frames() {
    const uint64_t now = GetTickCount64();
    bool changed = false;
    for (size_t monk = 0; monk < animators_.size(); ++monk) {
        const int index = static_cast<int>(monk);
        const bool singing = choir_.monk_singing(index);
        int frame =
            animators_[monk].frame(singing, singing ? choir_.monk_vowel(index) : 0.5f, now);
        // Closed lips (D16): sheet column 0 holds the closed mouth, and the
        // same row keeps the head pose, so frame % 6.
        if (singing && frame >= 6 && choir_.monk_lips_closed(index))
            frame %= 6;
        changed = changed || frame != frames_[monk];
        frames_[monk] = frame;
    }
    return changed;
}

// Equal cells side by side, centred, so every monk has the same size.
RECT ChoirWindow::monk_rect(const RECT &client, int monk) const {
    const int count = std::max(1, static_cast<int>(frames_.size()));
    const int cell_width = (client.right - client.left) / count;
    const int left = client.left + (client.right - client.left - cell_width * count) / 2 +
                     monk * cell_width;
    const RECT cell = {left, client.top, left + cell_width, client.bottom};
    return fit_rect(cell, kFaceWidth, kFaceHeight, options_.keep_aspect);
}

void ChoirWindow::paint() {
    PAINTSTRUCT paint_struct = {};
    HDC target = BeginPaint(hwnd_, &paint_struct);
    RECT client = {};
    GetClientRect(hwnd_, &client);

    HDC buffer = buffer_.prepare(target, client.right, client.bottom);
    if (buffer) {
        FillRect(buffer, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        for (size_t monk = 0; monk < frames_.size(); ++monk) {
            const int frame = frames_[monk];
            cache_.draw(target, buffer, monk_rect(client, static_cast<int>(monk)),
                        faces_.frame(frame), frame, options_.smooth);
        }
        buffer_.present(target);
    }
    EndPaint(hwnd_, &paint_struct);
}

} // namespace delay_lama::voice
