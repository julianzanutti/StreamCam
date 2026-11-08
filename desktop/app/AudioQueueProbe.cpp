#include "../media/LatestAudio.h"
#include <cstdio>

int main() {
    streamcam::LatestAudio queue;
    for (std::uint64_t timestamp = 0; timestamp <= streamcam::LatestAudio::MaxQueuedFrames; ++timestamp) {
        auto frame = std::make_shared<streamcam::AudioFrame>();
        frame->sampleRate = 48000;
        frame->channels = 1;
        frame->frames = 1;
        frame->timestampUs = timestamp;
        frame->pcm.push_back(static_cast<std::int16_t>(timestamp));
        queue.publish(frame);
    }
    if (queue.dropped() != 1) return 1;
    if (queue.queuedFrames() != streamcam::LatestAudio::MaxQueuedFrames) return 5;
    const auto firstBatch = queue.drain(5);
    if (firstBatch.size() != 5 || firstBatch.front()->timestampUs != 1 || firstBatch.back()->timestampUs != 5 || queue.queuedFrames()!=streamcam::LatestAudio::MaxQueuedFrames-5) return 2;
    const auto remaining = queue.drain();
    if (remaining.size() != streamcam::LatestAudio::MaxQueuedFrames - 5 || remaining.back()->timestampUs != 100) return 3;
    if (!queue.drain().empty() || queue.dropped() != 1 || queue.queuedFrames()!=0) return 4;
    std::puts("LatestAudio: bounded FIFO, overflow accounting, and partial drain passed");
    return 0;
}
