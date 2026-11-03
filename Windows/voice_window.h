#pragma once

#include "app.h"
#include "voice_choir.h"

#include <string>
#include <vector>

namespace delay_lama::voice {

struct WindowOptions {
    // Client area in physical screen pixels, like the standalone's windows.
    bool has_position = false;
    int x = 0;
    int y = 0;
    int width = 0; // 0: a default size for the choir
    int height = 0;
    bool topmost = true;
    bool title_bar = true;
    bool keep_aspect = true;
    bool smooth = false;
    std::wstring title = L"Delay Lama Voice";
};

// One window showing the choir's monks side by side, each animated from its
// own synth like the standalone's monk window, with its own idle clock. The
// monks are not interactive. Without a title bar the window drags to move and
// resizes at its edges, like the standalone's borderless monk window. Closing
// it ends the helper.
class ChoirWindow {
public:
    ChoirWindow(const Choir &choir, const FaceFrames &faces, WindowOptions options);
    ~ChoirWindow();
    ChoirWindow(const ChoirWindow &) = delete;
    ChoirWindow &operator=(const ChoirWindow &) = delete;

    bool create(HINSTANCE instance);
    HWND hwnd() const { return hwnd_; }

private:
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM w_param,
                                        LPARAM l_param);
    LRESULT handle_message(UINT message, WPARAM w_param, LPARAM l_param);
    // True if a monk's frame changed.
    bool update_frames();
    RECT monk_rect(const RECT &client, int monk) const;
    void paint();

    const Choir &choir_;
    const FaceFrames &faces_;
    WindowOptions options_;
    std::vector<MonkAnimator> animators_;
    std::vector<int> frames_;
    BackBuffer buffer_;
    // Every monk is drawn at the same size, so they share one cache.
    FaceCache cache_;
    HWND hwnd_ = nullptr;
};

} // namespace delay_lama::voice
