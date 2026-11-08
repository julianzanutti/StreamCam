#pragma once
#include <cstdint>
#include <vector>

namespace streamcam {
struct AudioFrame final {
    std::uint32_t sampleRate = 0;
    std::uint32_t channels = 0;
    std::uint32_t frames = 0;
    std::uint64_t timestampUs = 0;
    std::vector<std::int16_t> pcm;
    std::vector<float> samplesFloat;
    bool floatingPoint = false;
    bool valid() const {
        return sampleRate == 48000 && (channels == 1 || channels == 2) && frames > 0 && frames <= 960 &&
            (floatingPoint ? samplesFloat.size() == static_cast<std::size_t>(frames) * channels :
             pcm.size() == static_cast<std::size_t>(frames) * channels);
    }
};
}
