#pragma once
#include "AudioFrame.h"
#include <memory>
#include <mutex>

namespace streamcam {
class LatestAudio final {
public:
    void publish(std::shared_ptr<const AudioFrame> audio) {
        if (!audio || !audio->valid()) return;
        std::lock_guard lock(mutex_); latest_ = std::move(audio);
    }
    std::shared_ptr<const AudioFrame> read() const {
        std::lock_guard lock(mutex_); return latest_;
    }
    void clear() { std::lock_guard lock(mutex_); latest_.reset(); }
private:
    mutable std::mutex mutex_;
    std::shared_ptr<const AudioFrame> latest_;
};
}
