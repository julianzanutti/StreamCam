#include "../media/UsbH264DecodeQueue.h"
#include <cstdio>

namespace {
streamcam::UsbEncodedVideo packet(std::uint32_t timestamp, bool idr) {
    streamcam::UsbEncodedVideo value;
    value.width = 640; value.height = 360; value.rtpTimestamp = timestamp;
    value.timestampUs = static_cast<std::uint64_t>(timestamp) * 1000;
    value.bytes = {0, 0, 0, 1, static_cast<std::uint8_t>(idr ? 0x65 : 0x41), 0x88};
    return value;
}
}

int main() {
    streamcam::UsbH264DecodeQueue queue(2, 32);
    if (queue.push(packet(1, false))) return 1; // Must wait for an IDR after session start.
    if (!queue.push(packet(2, true)) || !queue.push(packet(3, false))) return 2;
    if (queue.push(packet(4, false))) return 3; // Overflow clears stale inter frames and requests a keyframe.
    if (queue.size() != 0 || queue.dropped() < 3) return 4;
    if (queue.push(packet(5, false))) return 5;
    if (!queue.push(packet(6, true)) || queue.size() != 1) return 6;
    auto decoded = queue.waitPop();
    if (!decoded || decoded->rtpTimestamp != 6 || decoded->timestampUs != 6000 || decoded->width != 640 || decoded->height != 360) return 7;
    const std::uint8_t lengthPrefixedIdr[]{0, 0, 0, 2, 0x65, 0x88};
    if (!streamcam::h264AccessUnitHasIdr(lengthPrefixedIdr, sizeof lengthPrefixedIdr)) return 8;
    queue.close();
    if (queue.waitPop()) return 9;
    std::puts("USB H264 decode queue: bounded overflow, IDR resync, and close passed");
    return 0;
}
