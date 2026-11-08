#include "../media/AudioSignalMonitor.h"
#include <cstdio>

int main() {
    streamcam::AudioSignalMonitor monitor;
    constexpr std::uint64_t second = 1'000'000'000ULL;
    if (monitor.underflow(10 * second)) return 1; // Video-only / no audio track.
    monitor.observe(second);
    if (monitor.underflow(second + 499'999'999ULL)) return 2;
    if (!monitor.underflow(second + 500'000'000ULL)) return 3;
    monitor.observe(second + 600'000'000ULL); // A silent PCM block is still a received block.
    if (monitor.underflow(second + 1'099'999'999ULL)) return 4;
    if (!monitor.underflow(second + 1'100'000'000ULL)) return 5;
    monitor.reset();
    if (monitor.underflow(10 * second)) return 6;
    std::puts("AudioSignalMonitor: no-track guard, timeout, recovery, and reset passed");
    return 0;
}
