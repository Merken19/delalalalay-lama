#include "app.h"

#include <algorithm>

namespace delay_lama {

float clamp01(float value) {
    return value > 0.0f ? (value < 1.0f ? value : 1.0f) : 0.0f;
}

std::wstring module_directory() {
    wchar_t path[MAX_PATH] = {};
    DWORD length = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
    if (length == 0)
        return L".";
    std::wstring full_path(path, length);
    const size_t slash = full_path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : full_path.substr(0, slash);
}

bool file_exists(const std::wstring &path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

// Newer DPI APIs are looked up at run time so the exe still starts on early
// Windows 10 builds that lack them.
FARPROC user32_proc(const char *name) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    return user32 ? GetProcAddress(user32, name) : nullptr;
}

void enable_per_monitor_dpi_awareness() {
    using SetContextFn = BOOL(WINAPI *)(DPI_AWARENESS_CONTEXT);
    const auto set_context =
        reinterpret_cast<SetContextFn>(user32_proc("SetProcessDpiAwarenessContext"));
    if (!set_context || !set_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        SetProcessDPIAware();
}

UINT dpi_for_window(HWND hwnd) {
    using GetDpiForWindowFn = UINT(WINAPI *)(HWND);
    static const auto get_dpi_for_window =
        reinterpret_cast<GetDpiForWindowFn>(user32_proc("GetDpiForWindow"));
    if (hwnd && get_dpi_for_window) {
        const UINT dpi = get_dpi_for_window(hwnd);
        if (dpi != 0)
            return dpi;
    }
    HDC screen = GetDC(nullptr);
    const int dpi = screen ? GetDeviceCaps(screen, LOGPIXELSX) : 96;
    if (screen)
        ReleaseDC(nullptr, screen);
    return dpi > 0 ? static_cast<UINT>(dpi) : 96u;
}

UINT dpi_for_rect(const RECT &rect) {
    using GetDpiForMonitorFn = HRESULT(WINAPI *)(HMONITOR, int, UINT *, UINT *);
    static const auto get_dpi_for_monitor = []() -> GetDpiForMonitorFn {
        HMODULE shcore = LoadLibraryExW(L"shcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        return shcore ? reinterpret_cast<GetDpiForMonitorFn>(
                            GetProcAddress(shcore, "GetDpiForMonitor"))
                      : nullptr;
    }();
    if (get_dpi_for_monitor) {
        HMONITOR monitor = MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST);
        UINT dpi_x = 0;
        UINT dpi_y = 0;
        // 0 is MDT_EFFECTIVE_DPI.
        if (SUCCEEDED(get_dpi_for_monitor(monitor, 0, &dpi_x, &dpi_y)) && dpi_x != 0)
            return dpi_x;
    }
    return dpi_for_window(nullptr);
}

int scale_px(int value, UINT dpi) { return MulDiv(value, static_cast<int>(dpi), 96); }

void adjust_window_rect_for_dpi(RECT &rect, DWORD style, DWORD ex_style, UINT dpi) {
    using AdjustFn = BOOL(WINAPI *)(LPRECT, DWORD, BOOL, DWORD, UINT);
    static const auto adjust =
        reinterpret_cast<AdjustFn>(user32_proc("AdjustWindowRectExForDpi"));
    if (adjust)
        adjust(&rect, style, FALSE, ex_style, dpi);
    else
        AdjustWindowRectEx(&rect, style, FALSE, ex_style);
}

RECT fit_rect(const RECT &bounds, int content_width, int content_height, bool keep_aspect) {
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    if (!keep_aspect || content_width <= 0 || content_height <= 0 || width <= 0 || height <= 0)
        return bounds;
    int fitted_width = width;
    int fitted_height = MulDiv(width, content_height, content_width);
    if (fitted_height > height) {
        fitted_height = height;
        fitted_width = MulDiv(height, content_width, content_height);
    }
    RECT fitted = {};
    fitted.left = bounds.left + (width - fitted_width) / 2;
    fitted.top = bounds.top + (height - fitted_height) / 2;
    fitted.right = fitted.left + fitted_width;
    fitted.bottom = fitted.top + fitted_height;
    return fitted;
}

LRESULT move_or_resize_hit_test(const RECT &client, POINT point, int margin) {
    const bool left = point.x < margin;
    const bool right = point.x >= client.right - margin;
    const bool top = point.y < margin;
    const bool bottom = point.y >= client.bottom - margin;
    if (top && left)
        return HTTOPLEFT;
    if (top && right)
        return HTTOPRIGHT;
    if (bottom && left)
        return HTBOTTOMLEFT;
    if (bottom && right)
        return HTBOTTOMRIGHT;
    if (left)
        return HTLEFT;
    if (right)
        return HTRIGHT;
    if (top)
        return HTTOP;
    if (bottom)
        return HTBOTTOM;
    return HTCAPTION;
}

std::wstring note_name(int midi_note) {
    static const wchar_t *const kNames[] = {L"C",  L"C#", L"D",  L"D#", L"E",  L"F",
                                            L"F#", L"G",  L"G#", L"A",  L"A#", L"B"};
    midi_note = std::max(0, std::min(127, midi_note));
    return std::wstring(kNames[midi_note % 12]) + std::to_wstring(midi_note / 12 - 1);
}

std::unique_ptr<Gdiplus::Bitmap> load_bitmap(const std::wstring &path) {
    if (!file_exists(path))
        return nullptr;
    auto bitmap = std::make_unique<Gdiplus::Bitmap>(path.c_str(), FALSE);
    if (bitmap->GetLastStatus() != Gdiplus::Ok)
        return nullptr;
    return bitmap;
}

// Copies part of an image into a 32-bpp premultiplied bitmap, the fastest
// format for GDI+ to scale. The original PNGs are palettized.
std::unique_ptr<Gdiplus::Bitmap> copy_region(Gdiplus::Image &source, int x, int y, int width,
                                             int height) {
    auto bitmap = std::make_unique<Gdiplus::Bitmap>(width, height, PixelFormat32bppPARGB);
    if (bitmap->GetLastStatus() != Gdiplus::Ok)
        return nullptr;
    Gdiplus::Graphics graphics(bitmap.get());
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    if (graphics.DrawImage(&source, Gdiplus::Rect(0, 0, width, height), x, y, width, height,
                           Gdiplus::UnitPixel) != Gdiplus::Ok)
        return nullptr;
    return bitmap;
}

void draw_image_scaled(Gdiplus::Graphics &graphics, Gdiplus::Image *image,
                       const Gdiplus::Rect &destination, bool smooth) {
    if (!image)
        return;
    graphics.SetInterpolationMode(smooth ? Gdiplus::InterpolationModeHighQualityBicubic
                                         : Gdiplus::InterpolationModeNearestNeighbor);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    // Mirrored edge sampling keeps the filter from darkening the outer pixels.
    Gdiplus::ImageAttributes attributes;
    attributes.SetWrapMode(Gdiplus::WrapModeTileFlipXY);
    graphics.DrawImage(image, destination, 0, 0, static_cast<INT>(image->GetWidth()),
                       static_cast<INT>(image->GetHeight()), Gdiplus::UnitPixel, &attributes);
}

HBITMAP render_scaled(HDC reference, Gdiplus::Image *image, int width, int height, bool smooth) {
    if (!image || width <= 0 || height <= 0)
        return nullptr;
    HBITMAP bitmap = CreateCompatibleBitmap(reference, width, height);
    if (!bitmap)
        return nullptr;
    HDC dc = CreateCompatibleDC(reference);
    HGDIOBJ previous = SelectObject(dc, bitmap);
    {
        Gdiplus::Graphics graphics(dc);
        draw_image_scaled(graphics, image, Gdiplus::Rect(0, 0, width, height), smooth);
    }
    SelectObject(dc, previous);
    DeleteDC(dc);
    return bitmap;
}

void blit_bitmap(HDC target, int x, int y, int width, int height, HBITMAP bitmap) {
    HDC source = CreateCompatibleDC(target);
    HGDIOBJ previous = SelectObject(source, bitmap);
    BitBlt(target, x, y, width, height, source, 0, 0, SRCCOPY);
    SelectObject(source, previous);
    DeleteDC(source);
}

// One line per semitone at the exact pitch of that note (the centre of its
// zone when snapping), brighter on naturals and brightest with a label on C.
void draw_note_grid(Gdiplus::Graphics &graphics, const RECT &area, const FlagSettings &flag) {
    const float span = static_cast<float>(flag.high_note - flag.low_note);
    if (span <= 0.0f)
        return;
    const float left = static_cast<float>(area.left);
    const float top = static_cast<float>(area.top);
    const float width = static_cast<float>(area.right - area.left);
    const float height = static_cast<float>(area.bottom - area.top);
    const float label_size = std::max(9.0f, std::min(28.0f, height * 0.12f));

    Gdiplus::Pen sharp(Gdiplus::Color(40, 255, 255, 255), 1.0f);
    Gdiplus::Pen natural(Gdiplus::Color(90, 255, 255, 255), 1.0f);
    Gdiplus::Pen octave(Gdiplus::Color(190, 255, 255, 255), 2.0f);
    Gdiplus::Font font(L"Segoe UI", label_size, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush text(Gdiplus::Color(235, 255, 255, 255));
    Gdiplus::SolidBrush shadow(Gdiplus::Color(170, 0, 0, 0));

    for (int note = flag.low_note; note <= flag.high_note; ++note) {
        const float x = left + (static_cast<float>(note - flag.low_note) / span) * width;
        const int pitch_class = note % 12;
        const bool is_sharp = pitch_class == 1 || pitch_class == 3 || pitch_class == 6 ||
                              pitch_class == 8 || pitch_class == 10;
        Gdiplus::Pen *pen = pitch_class == 0 ? &octave : (is_sharp ? &sharp : &natural);
        graphics.DrawLine(pen, x, top, x, top + height);
        if (pitch_class == 0) {
            const std::wstring label = note_name(note);
            const float label_y = top + height - label_size * 1.45f;
            graphics.DrawString(label.c_str(), -1, &font,
                                Gdiplus::PointF(x + 4.0f, label_y + 1.0f), &shadow);
            graphics.DrawString(label.c_str(), -1, &font, Gdiplus::PointF(x + 3.0f, label_y),
                                &text);
        }
    }
}

// x and y are the drag position normalized to the area.
void draw_crosshair(Gdiplus::Graphics &graphics, const RECT &area, float x, float y) {
    const float left = static_cast<float>(area.left);
    const float top = static_cast<float>(area.top);
    const float width = static_cast<float>(area.right - area.left);
    const float height = static_cast<float>(area.bottom - area.top);
    const float marker_x = left + x * width;
    const float marker_y = top + y * height;
    const float radius = std::max(4.0f, std::min(width, height) * 0.035f);

    Gdiplus::Pen cross(Gdiplus::Color(185, 255, 255, 255), 1.0f);
    graphics.DrawLine(&cross, left, marker_y, left + width, marker_y);
    graphics.DrawLine(&cross, marker_x, top, marker_x, top + height);
    Gdiplus::SolidBrush marker(Gdiplus::Color(230, 255, 255, 255));
    graphics.FillEllipse(&marker, Gdiplus::RectF(marker_x - radius, marker_y - radius,
                                                 radius * 2.0f, radius * 2.0f));
}

HDC BackBuffer::prepare(HDC reference, int width, int height) {
    width = std::max(1, width);
    height = std::max(1, height);
    if (dc_ && width == width_ && height == height_)
        return dc_;
    release();
    dc_ = CreateCompatibleDC(reference);
    bitmap_ = CreateCompatibleBitmap(reference, width, height);
    if (!dc_ || !bitmap_) {
        release();
        return nullptr;
    }
    previous_ = SelectObject(dc_, bitmap_);
    width_ = width;
    height_ = height;
    return dc_;
}

void BackBuffer::present(HDC target) const {
    if (dc_)
        BitBlt(target, 0, 0, width_, height_, dc_, 0, 0, SRCCOPY);
}

void BackBuffer::release() {
    if (dc_) {
        if (previous_)
            SelectObject(dc_, previous_);
        DeleteDC(dc_);
    }
    if (bitmap_)
        DeleteObject(bitmap_);
    dc_ = nullptr;
    bitmap_ = nullptr;
    previous_ = nullptr;
    width_ = 0;
    height_ = 0;
}

} // namespace delay_lama
