#pragma once

#include <atomic>
#include <cstdint>

namespace streamcam {
// Thread-safe arrival monitor. Timestamps use steady-clock nanoseconds.
class AudioSignalMonitor final {
public:
    static constexpr std::uint64_t DefaultTimeoutNs = 500'000'000ULL;

    void observe(std::uint64_t nowNs) noexcept {
        lastArrivalNs_.store(nowNs, std::memory_order_relaxed);
        observed_.store(true, std::memory_order_release);
    }

    bool underflow(std::uint64_t nowNs, std::uint64_t timeoutNs = DefaultTimeoutNs) const noexcept {
        if (!observed_.load(std::memory_order_acquire)) return false;
        const auto last = lastArrivalNs_.load(std::memory_order_relaxed);
        return nowNs >= last && nowNs - last >= timeoutNs;
    }

    void reset() noexcept {
        observed_.store(false, std::memory_order_release);
        lastArrivalNs_.store(0, std::memory_order_relaxed);
    }

private:
    std::atomic<std::uint64_t> lastArrivalNs_{0};
    std::atomic<bool> observed_{false};
};
}
