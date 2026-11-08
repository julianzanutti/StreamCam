#pragma once

#include "AvTimestampMapper.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace streamcam {

// Coordinates the two OBS named-pipe connections around one capture-clock
// epoch. A reconnect may come from a different Android boot clock, so neither
// track is mapped until both pipes are ready and the mapper is rebased once.
class AvSessionClock final {
public:
    using Stream = AvTimestampMapper::Stream;

    void setConnected(Stream stream, bool connected) {
        std::lock_guard lock(mutex_);
        const bool wasReady = ready_;
        connected_[index(stream)] = connected;
        ready_ = connected_[index(Stream::Audio)] && connected_[index(Stream::Video)];
        if (!wasReady && ready_) mapper_.reset();
    }

    bool map(Stream stream, std::uint64_t senderUs, std::uint64_t nowNs,
             std::uint64_t &outputNs) {
        std::lock_guard lock(mutex_);
        if (!ready_) return false;
        return mapper_.map(stream, senderUs, nowNs, outputNs);
    }

private:
    static constexpr std::size_t index(Stream stream) {
        return static_cast<std::size_t>(stream);
    }

    std::mutex mutex_;
    AvTimestampMapper mapper_;
    std::array<bool, 2> connected_{};
    bool ready_ = false;
};

} // namespace streamcam
