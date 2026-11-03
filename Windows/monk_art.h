#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <memory>
#include <string>
#include <vector>

namespace delay_lama {

// The 30 monk frames cut out of Resources\faces.png (see the kFace* constants
// in app.h for the sheet geometry).
class FaceFrames {
public:
    bool load(const std::wstring &faces_png);
    Gdiplus::Bitmap *frame(int index) const;

private:
    std::vector<std::unique_ptr<Gdiplus::Bitmap>> frames_;
};

// Pre-scaled copies of the monk frames at one size, so playback is a plain
// BitBlt. Above kFrameCacheBudgetBytes (very large windows) frames are scaled
// on the fly instead. Every monk drawn at the same size can share one cache.
class FaceCache {
public:
    FaceCache();
    ~FaceCache() { clear(); }
    FaceCache(const FaceCache &) = delete;
    FaceCache &operator=(const FaceCache &) = delete;

    void clear();
    // Draws `image`, monk frame `frame`, scaled into `area` of `buffer`.
    // `reference` is the DC the cached bitmaps are made compatible with.
    void draw(HDC reference, HDC buffer, const RECT &area, Gdiplus::Bitmap *image, int frame,
              bool smooth);

private:
    std::vector<HBITMAP> cache_;
    int width_ = 0;
    int height_ = 0;
};

} // namespace delay_lama
