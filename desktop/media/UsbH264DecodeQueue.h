#pragma once

#include <condition_variable>
#include <algorithm>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

namespace streamcam {
struct UsbEncodedVideo {
    int width{};
    int height{};
    std::uint64_t timestampUs{};
    std::uint32_t rtpTimestamp{};
    std::vector<std::uint8_t> bytes;
};

inline bool h264AccessUnitHasIdr(const std::uint8_t *data, std::size_t size) noexcept {
    if (!data || size < 5) return false;
    for (std::size_t i = 0; i + 3 < size; ++i) {
        std::size_t nal = size;
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) nal = i + 3;
        else if (i + 4 < size && data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 0 && data[i + 3] == 1) nal = i + 4;
        if (nal < size && (data[nal] & 0x1f) == 5) return true;
    }
    // Also accept AVC length-prefixed NAL units (4-byte big-endian lengths).
    for (std::size_t offset = 0; offset + 4 <= size;) {
        const auto length = (std::uint32_t(data[offset]) << 24) | (std::uint32_t(data[offset + 1]) << 16) |
            (std::uint32_t(data[offset + 2]) << 8) | std::uint32_t(data[offset + 3]);
        offset += 4;
        if (length == 0 || length > size - offset) return false;
        if ((data[offset] & 0x1f) == 5) return true;
        offset += length;
    }
    return false;
}

class UsbH264DecodeQueue final {
public:
    explicit UsbH264DecodeQueue(std::size_t maxFrames = 3, std::size_t maxBytes = 8 * 1024 * 1024)
        : maxFrames_(std::max<std::size_t>(1, maxFrames)), maxBytes_(std::max<std::size_t>(1, maxBytes)) {}
    UsbH264DecodeQueue(const UsbH264DecodeQueue &) = delete;
    UsbH264DecodeQueue &operator=(const UsbH264DecodeQueue &) = delete;

    bool push(UsbEncodedVideo packet) {
        const bool keyframe = h264AccessUnitHasIdr(packet.bytes.data(), packet.bytes.size());
        std::lock_guard lock(mutex_);
        if (closed_) return false;
        if (packet.width <= 0 || packet.height <= 0 || packet.bytes.empty() || packet.bytes.size() > maxBytes_) {
            ++dropped_; waitingForKeyframe_ = true; return false;
        }
        if (activeWidth_ && (packet.width != activeWidth_ || packet.height != activeHeight_)) {
            dropped_ += queue_.size(); queue_.clear(); queuedBytes_ = 0; waitingForKeyframe_ = true;
        }
        if (waitingForKeyframe_ && !keyframe) { ++dropped_; return false; }
        if (queue_.size() >= maxFrames_ || packet.bytes.size() > maxBytes_ - queuedBytes_) {
            dropped_ += queue_.size();
            queue_.clear(); queuedBytes_ = 0;
            waitingForKeyframe_ = true;
            if (!keyframe) { ++dropped_; return false; }
        }
        activeWidth_ = packet.width; activeHeight_ = packet.height;
        waitingForKeyframe_ = false;
        queuedBytes_ += packet.bytes.size();
        queue_.push_back(std::move(packet));
        ready_.notify_one();
        return true;
    }

    std::optional<UsbEncodedVideo> waitPop() {
        std::unique_lock lock(mutex_);
        ready_.wait(lock, [this] { return closed_ || !queue_.empty(); });
        if (queue_.empty()) return std::nullopt;
        auto packet = std::move(queue_.front());
        queuedBytes_ -= packet.bytes.size();
        queue_.pop_front();
        return packet;
    }

    void requireKeyframe() {
        std::lock_guard lock(mutex_);
        dropped_ += queue_.size(); queue_.clear(); queuedBytes_ = 0; waitingForKeyframe_ = true;
    }
    void close() {
        std::lock_guard lock(mutex_);
        closed_ = true; queue_.clear(); queuedBytes_ = 0; ready_.notify_all();
    }
    std::uint64_t dropped() const { std::lock_guard lock(mutex_); return dropped_; }
    std::size_t size() const { std::lock_guard lock(mutex_); return queue_.size(); }

private:
    const std::size_t maxFrames_;
    const std::size_t maxBytes_;
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<UsbEncodedVideo> queue_;
    std::size_t queuedBytes_ = 0;
    std::uint64_t dropped_ = 0;
    int activeWidth_ = 0;
    int activeHeight_ = 0;
    bool waitingForKeyframe_ = true;
    bool closed_ = false;
};
}
