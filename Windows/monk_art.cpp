#include "monk_art.h"

#include "app.h"

#include <cstdint>

namespace delay_lama {

bool FaceFrames::load(const std::wstring &faces_png) {
    frames_.clear();
    auto faces = load_bitmap(faces_png);
    if (!faces)
        return false;
    for (int frame = 0; frame < kFaceFrames; ++frame) {
        const int column = frame / kFaceRowsPerColumn;
        const int row = frame % kFaceRowsPerColumn;
        auto bitmap = copy_region(*faces, column * kFaceCellWidth, row * kFaceCellHeight,
                                  kFaceWidth, kFaceHeight);
        if (!bitmap) {
            frames_.clear();
            return false;
        }
        frames_.push_back(std::move(bitmap));
    }
    return true;
}

Gdiplus::Bitmap *FaceFrames::frame(int index) const {
    if (index < 0 || index >= static_cast<int>(frames_.size()))
        return nullptr;
    return frames_[static_cast<size_t>(index)].get();
}

FaceCache::FaceCache() : cache_(kFaceFrames, nullptr) {}

void FaceCache::clear() {
    for (HBITMAP &bitmap : cache_) {
        if (bitmap)
            DeleteObject(bitmap);
        bitmap = nullptr;
    }
    width_ = 0;
    height_ = 0;
}

void FaceCache::draw(HDC reference, HDC buffer, const RECT &area, Gdiplus::Bitmap *image,
                     int frame, bool smooth) {
    const int width = area.right - area.left;
    const int height = area.bottom - area.top;
    if (!image || width <= 0 || height <= 0 || frame < 0 || frame >= kFaceFrames)
        return;
    if (width != width_ || height != height_) {
        clear();
        width_ = width;
        height_ = height;
    }
    const bool cacheable =
        static_cast<int64_t>(width) * height * 4 * kFaceFrames <= kFrameCacheBudgetBytes;
    HBITMAP &cached = cache_[static_cast<size_t>(frame)];
    if (cacheable && !cached)
        cached = render_scaled(reference, image, width, height, smooth);
    if (cacheable && cached) {
        blit_bitmap(buffer, area.left, area.top, width, height, cached);
    } else {
        Gdiplus::Graphics graphics(buffer);
        draw_image_scaled(graphics, image, Gdiplus::Rect(area.left, area.top, width, height),
                          smooth);
    }
}

} // namespace delay_lama
