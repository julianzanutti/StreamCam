#pragma once
#include <cstdint>
#include <limits>

namespace streamcam {
// Converts sender microseconds to OBS monotonic nanoseconds. Long sessions do
// not reset; only duplicate, regressed, discontinuous, or overflowing clocks rebase.
class TimestampMapper final {
public:
    bool map(std::uint64_t senderUs, std::uint64_t nowNs, std::uint64_t &outputNs) {
        if (initialized_ && senderUs == previousSenderUs_) return false;
        const bool senderRegressed = initialized_ && senderUs < previousSenderUs_;
        const bool senderJumped = initialized_ && !senderRegressed && senderUs - previousSenderUs_ > MaxStepUs;
        if (!initialized_ || senderRegressed || senderJumped || senderUs < originUs_) rebase(senderUs, nowNs);

        auto relativeUs = senderUs - originUs_;
        if (relativeUs > (std::numeric_limits<std::uint64_t>::max() - baseNs_) / 1000ULL)
            rebase(senderUs, nowNs);
        relativeUs = senderUs - originUs_;
        outputNs = baseNs_ + relativeUs * 1000ULL;
        if (initialized_ && outputNs <= previousOutputNs_) {
            rebase(senderUs, nowNs);
            outputNs = baseNs_;
        }
        previousSenderUs_ = senderUs;
        previousOutputNs_ = outputNs;
        initialized_ = true;
        return true;
    }

private:
    static constexpr std::uint64_t MaxStepUs = 10'000'000ULL;
    std::uint64_t originUs_ = 0;
    std::uint64_t baseNs_ = 0;
    std::uint64_t previousSenderUs_ = 0;
    std::uint64_t previousOutputNs_ = 0;
    bool initialized_ = false;

    void rebase(std::uint64_t senderUs, std::uint64_t nowNs) {
        originUs_ = senderUs;
        baseNs_ = nowNs;
        if (initialized_ && previousOutputNs_ >= baseNs_ && previousOutputNs_ != std::numeric_limits<std::uint64_t>::max())
            baseNs_ = previousOutputNs_ + 1;
    }
};
}
