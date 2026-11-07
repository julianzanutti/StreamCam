#pragma once
#include <cstdint>
#include <vector>

namespace streamcam {
// Owned BGRA frame for the first measured RAM path to Qt and OBS IPC.
// GPU decode/shared textures remain a separate implementation and measurement.
struct VideoFrame {
    int width = 0;
    int height = 0;
    int stride = 0;
    std::int64_t timestampUs = 0;
    std::vector<std::uint8_t> bgra;
    bool valid() const {
        return width > 0 && width <= 1920 && height > 0 && height <= 1080 &&
            stride >= width * 4 && stride <= 1920 * 4 &&
            bgra.size() == static_cast<std::size_t>(stride) * height;
    }
};
}
