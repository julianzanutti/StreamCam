#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <limits>

namespace streamcam {

// Maps independently delivered audio/video timestamps onto one OBS monotonic
// timeline. Both sender clocks are in microseconds on the same capture clock.
class AvTimestampMapper final {
public:
    enum class Stream : std::size_t { Audio, Video };

    bool map(Stream stream, std::uint64_t senderUs, std::uint64_t nowNs,
             std::uint64_t &outputNs) {
        std::lock_guard lock(mutex_);
        auto &track = tracks_[static_cast<std::size_t>(stream)];
        if (track.initialized && senderUs <= track.previousSenderUs) return false;
        if (!initialized_) {
            originUs_ = senderUs;
            originNs_ = nowNs;
            initialized_ = true;
        }

        if (senderUs >= originUs_) {
            const auto deltaUs = senderUs - originUs_;
            if (deltaUs > (std::numeric_limits<std::uint64_t>::max() - originNs_) / 1000ULL)
                return false;
            outputNs = originNs_ + deltaUs * 1000ULL;
        } else {
            const auto deltaUs = originUs_ - senderUs;
            if (deltaUs > originNs_ / 1000ULL) return false;
            outputNs = originNs_ - deltaUs * 1000ULL;
        }
        if (track.initialized && outputNs <= track.previousOutputNs) return false;
        track.previousSenderUs = senderUs;
        track.previousOutputNs = outputNs;
        track.initialized = true;
        return true;
    }

    void reset() {
        std::lock_guard lock(mutex_);
        originUs_ = originNs_ = 0;
        initialized_ = false;
        tracks_ = {};
    }

private:
    struct Track {
        std::uint64_t previousSenderUs = 0;
        std::uint64_t previousOutputNs = 0;
        bool initialized = false;
    };

    std::mutex mutex_;
    std::array<Track, 2> tracks_{};
    std::uint64_t originUs_ = 0;
    std::uint64_t originNs_ = 0;
    bool initialized_ = false;
};

} // namespace streamcam
