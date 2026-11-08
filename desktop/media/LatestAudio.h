#pragma once
#include "AudioFrame.h"
#include <memory>
#include <mutex>
#include <deque>
#include <vector>
#include <algorithm>

namespace streamcam {
class LatestAudio final {
public:
    static constexpr std::size_t MaxQueuedFrames = 100;
    void publish(std::shared_ptr<const AudioFrame> audio) {
        if (!audio || !audio->valid()) return;
        std::lock_guard lock(mutex_);
        if (queue_.size() == MaxQueuedFrames) { queue_.pop_front(); ++dropped_; }
        queue_.push_back(std::move(audio));
    }
    std::vector<std::shared_ptr<const AudioFrame>> drain(std::size_t maximum = MaxQueuedFrames) {
        std::lock_guard lock(mutex_);
        std::vector<std::shared_ptr<const AudioFrame>> result;
        const auto count=std::min(maximum,queue_.size());
        result.reserve(count);
        for(std::size_t i=0;i<count;++i) { result.push_back(std::move(queue_.front())); queue_.pop_front(); }
        return result;
    }
    std::uint64_t dropped() const { std::lock_guard lock(mutex_); return dropped_; }
    std::size_t queuedFrames() const { std::lock_guard lock(mutex_); return queue_.size(); }
    void clear() { std::lock_guard lock(mutex_); queue_.clear(); }
private:
    mutable std::mutex mutex_;
    std::deque<std::shared_ptr<const AudioFrame>> queue_;
    std::uint64_t dropped_ = 0;
};
}
