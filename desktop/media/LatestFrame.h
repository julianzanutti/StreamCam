#pragma once
#include "VideoFrame.h"
#include <memory>
#include <mutex>

namespace streamcam {
// One slot: slow UI/OBS never builds an unbounded frame backlog.
class LatestFrame final {
public:
    void publish(std::shared_ptr<const VideoFrame> frame) {
        if (!frame || !frame->valid()) return;
        std::lock_guard lock(mutex_);
        latest_ = std::move(frame);
    }
    std::shared_ptr<const VideoFrame> read() const {
        std::lock_guard lock(mutex_); return latest_;
    }
    void clear() { std::lock_guard lock(mutex_); latest_.reset(); }
private:
    mutable std::mutex mutex_;
    std::shared_ptr<const VideoFrame> latest_;
};
}
