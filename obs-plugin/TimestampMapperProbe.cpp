#include "TimestampMapper.h"
#include <cstdint>
#include <cstdio>

int main() {
    streamcam::TimestampMapper mapper;
    std::uint64_t output = 0, previous = 0;
    constexpr std::uint64_t baseNs = 50'000'000'000ULL;
    for (std::uint64_t second = 0; second <= 3601; ++second) {
        if (!mapper.map(second * 1'000'000ULL, baseNs + second * 1'000'000'000ULL, output)) return 1;
        if (second && output <= previous) return 2;
        previous = output;
    }
    if (mapper.map(3601'000'000ULL, baseNs + 3601'000'000'000ULL, output)) return 3;

    if (!mapper.map(100, baseNs + 3602'000'000'000ULL, output) || output <= previous) return 4;
    previous = output;
    if (!mapper.map(10'000'101, baseNs + 3603'000'000'000ULL, output) || output <= previous) return 5;

    std::puts("TimestampMapper: >1 h, duplicate, regression, and discontinuity passed");
    return 0;
}
