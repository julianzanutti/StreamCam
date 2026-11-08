#pragma once

#include <cstddef>
#include <deque>
#include <string>
#include <unordered_set>

namespace streamcam {

class AudioEffectsAckTracker final {
public:
    enum class TimeoutAction { Stale, Retry, Expired };
    enum class AckAction { Accepted, Duplicate, Unexpected };

    bool begin(const std::string &requestId) {
        if (requestId.empty() || !pending_.empty()) return false;
        pending_ = requestId;
        retries_ = 0;
        return true;
    }

    TimeoutAction timeout(const std::string &requestId) {
        if (pending_ != requestId) return TimeoutAction::Stale;
        if (retries_ < MaxRetries) {
            ++retries_;
            return TimeoutAction::Retry;
        }
        remember(pending_);
        pending_.clear();
        retries_ = 0;
        return TimeoutAction::Expired;
    }

    AckAction acknowledge(const std::string &requestId) {
        if (!pending_.empty() && pending_ == requestId) {
            remember(pending_);
            pending_.clear();
            retries_ = 0;
            return AckAction::Accepted;
        }
        return recent_.contains(requestId) ? AckAction::Duplicate : AckAction::Unexpected;
    }

    bool abandon(const std::string &requestId) {
        if (pending_ != requestId) return false;
        remember(pending_);
        pending_.clear();
        retries_ = 0;
        return true;
    }

    void reset() {
        pending_.clear();
        retries_ = 0;
        recent_.clear();
        recentOrder_.clear();
    }

    bool hasPending() const noexcept { return !pending_.empty(); }
    const std::string &pendingRequestId() const noexcept { return pending_; }

private:
    static constexpr unsigned MaxRetries = 1;
    static constexpr std::size_t RecentLimit = 128;
    std::string pending_;
    unsigned retries_ = 0;
    std::deque<std::string> recentOrder_;
    std::unordered_set<std::string> recent_;

    void remember(const std::string &requestId) {
        if (!recent_.insert(requestId).second) return;
        recentOrder_.push_back(requestId);
        if (recentOrder_.size() > RecentLimit) {
            recent_.erase(recentOrder_.front());
            recentOrder_.pop_front();
        }
    }
};

} // namespace streamcam
