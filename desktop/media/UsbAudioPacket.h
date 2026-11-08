#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace streamcam {
struct UsbAudioPacketView {
    std::uint32_t sampleRate{};
    std::uint32_t channels{};
    std::uint32_t frames{};
    std::uint64_t timestampUs{};
    const std::uint8_t *pcm{};
    std::size_t pcmBytes{};
};

inline std::optional<UsbAudioPacketView> parseUsbAudioPacket(const void *raw, std::size_t size) noexcept {
    constexpr std::size_t headerBytes = 32;
    if (!raw || size < headerBytes) return std::nullopt;
    const auto *data = static_cast<const std::uint8_t *>(raw);
    const auto u32 = [data](std::size_t at) {
        return std::uint32_t(data[at]) | (std::uint32_t(data[at + 1]) << 8) |
            (std::uint32_t(data[at + 2]) << 16) | (std::uint32_t(data[at + 3]) << 24);
    };
    const auto u64 = [data](std::size_t at) {
        std::uint64_t value = 0;
        for (unsigned i = 0; i < 8; ++i) value |= std::uint64_t(data[at + i]) << (i * 8);
        return value;
    };
    if (u32(0) != 0x31414353 || u32(4) != 1 || u32(8) != 48000 ||
        u32(12) != 1 || u32(16) == 0 || u32(16) > 960 || u32(28) != 0) return std::nullopt;
    const auto payloadBytes = std::size_t(u32(16)) * 2;
    if (size != headerBytes + payloadBytes) return std::nullopt;
    return UsbAudioPacketView{u32(8), u32(12), u32(16), u64(20), data + headerBytes, payloadBytes};
}
}
