#include "voice_window.h"

#include "resource.h"

#include <windowsx.h>

namespace delay_lama::voice {
namespace {

const wchar_t kWindowClass[] = L"DelayLamaVoiceWindow";
// Default size of one monk, in pixels at 96 DPI.
constexpr int kDefaultMonkHeight = 160;
// The label strip, in pixels at 96 DPI: its height and the text's.
constexpr int kLabelHeight = 20;
constexpr int kLabelTextHeight = 13;

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
    if (label_font_)
        DeleteObject(label_font_);
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
    if (!options_.label.empty()) {
        label_height_ = scale_px(kLabelHeight, dpi);
        label_font_ = CreateFontW(-scale_px(kLabelTextHeight, dpi), 0, 0, 0, FW_SEMIBOLD, FALSE,
                                  FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                                  L"Segoe UI");
    }
    const int width = options_.width > 0
                          ? options_.width
                          : choir_.monks() * MulDiv(monk_height, kFaceWidth, kFaceHeight);
    // A given height holds the label strip; the default one adds it.
    const int height = options_.height > 0 ? options_.height : monk_height + label_height_;
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

// The window keeps its place, size and process while hidden; showing it again
// puts it back on top (of the topmost windows, if it is one) without taking
// the focus, so a performance window that opened since can't cover it.
void ChoirWindow::follow_visibility() {
    const bool visible = choir_.window_visible();
    if (visible == shown_)
        return;
    shown_ = visible;
    if (!visible) {
        ShowWindow(hwnd_, SW_HIDE);
        return;
    }
    SetWindowPos(hwnd_, options_.topmost ? HWND_TOPMOST : HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
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
        if (w_param == kAnimationTimer) {
            follow_visibility();
            if (update_frames())
                InvalidateRect(hwnd_, nullptr, FALSE);
        }
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
    const LONG bottom = std::max(client.top, client.bottom - label_height_);
    const int cell_width = (client.right - client.left) / count;
    const int left = client.left + (client.right - client.left - cell_width * count) / 2 +
                     monk * cell_width;
    const RECT cell = {left, client.top, left + cell_width, bottom};
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
        paint_label(buffer, client);
        buffer_.present(target);
    }
    EndPaint(hwnd_, &paint_struct);
}

void ChoirWindow::paint_label(HDC buffer, const RECT &client) {
    if (label_height_ <= 0)
        return;
    RECT strip = {client.left, std::max(client.top, client.bottom - label_height_), client.right,
                  client.bottom};
    HBRUSH background = CreateSolidBrush(RGB(38, 34, 30));
    FillRect(buffer, &strip, background);
    DeleteObject(background);
    const int saved = SaveDC(buffer);
    if (label_font_)
        SelectObject(buffer, label_font_);
    SetBkMode(buffer, TRANSPARENT);
    SetTextColor(buffer, RGB(240, 222, 170));
    InflateRect(&strip, -scale_px(4, dpi_for_window(hwnd_)), 0);
    DrawTextW(buffer, options_.label.c_str(), static_cast<int>(options_.label.size()), &strip,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    RestoreDC(buffer, saved);
}

} // namespace delay_lama::voice
