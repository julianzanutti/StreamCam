#include "../media/UsbAudioPacket.h"
#include <array>
#include <cstdio>

int main() {
    std::array<std::uint8_t, 36> golden{
        0x53,0x43,0x41,0x31, 1,0,0,0, 0x80,0xbb,0,0, 1,0,0,0,
        2,0,0,0, 0x08,0x07,0x06,0x05,0x04,0x03,0x02,0x01, 0,0,0,0, 0x34,0x12,0xcd,0xab
    };
    const auto parsed = streamcam::parseUsbAudioPacket(golden.data(), golden.size());
    if (!parsed || parsed->sampleRate != 48000 || parsed->channels != 1 || parsed->frames != 2 ||
        parsed->timestampUs != 0x0102030405060708ULL || parsed->pcmBytes != 4 ||
        parsed->pcm[0] != 0x34 || parsed->pcm[3] != 0xab) return 1;
    if (streamcam::parseUsbAudioPacket(golden.data(), golden.size() - 1)) return 2;
    golden[28] = 1;
    if (streamcam::parseUsbAudioPacket(golden.data(), golden.size())) return 3;
    std::puts("USB audio SCA1: 32-byte header, LE timestamp, PCM boundary and malformed packet rejection passed");
    return 0;
}
