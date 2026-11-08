#include "../ipc/AudioPublisher.h"
#include "../ipc/FramePublisher.h"

#include <QCoreApplication>
#include <QTimer>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>

using Clock = std::chrono::steady_clock;

static std::uint64_t timestampUs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now().time_since_epoch()).count());
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto videoPipe = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("StreamCamFrames-v1");
    const auto audioPipe = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QStringLiteral("StreamCamAudio-v1");
    streamcam::FramePublisher video;
    streamcam::AudioPublisher audio;
    const bool videoStarted=video.start(videoPipe);
    const bool audioStarted=audio.start(audioPipe);
    if (!videoStarted || !audioStarted) {
        std::cerr << "Could not open the StreamCam OBS pipes: video=" << video.errorString().toStdString()
                  << " audio=" << audio.errorString().toStdString() << ".\n";
        return 2;
    }
    std::cerr << "Synthetic pipe servers: video=" << video.fullServerName().toStdString()
              << " audio=" << audio.fullServerName().toStdString()
              << " pid=" << QCoreApplication::applicationPid() << "\n";

    std::uint64_t videoFrames = 0;
    std::uint64_t audioFrames = 0;
    std::uint64_t videoDelivered = 0;
    std::uint64_t audioDelivered = 0;
    QTimer videoTimer;
    videoTimer.setInterval(33);
    QObject::connect(&videoTimer, &QTimer::timeout, &app, [&] {
        auto frame = std::make_shared<streamcam::VideoFrame>();
        frame->width = 64;
        frame->height = 48;
        frame->stride = 64 * 4;
        frame->timestampUs = timestampUs();
        frame->bgra.resize(frame->stride * frame->height);
        for (std::size_t i = 0; i < frame->bgra.size(); i += 4) {
            frame->bgra[i] = 40;
            frame->bgra[i + 1] = 80;
            frame->bgra[i + 2] = 120;
            frame->bgra[i + 3] = 255;
        }
        if (video.publish(frame)) ++videoDelivered;
        ++videoFrames;
    });

    QTimer audioTimer;
    audioTimer.setInterval(20);
    QObject::connect(&audioTimer, &QTimer::timeout, &app, [&] {
        auto frame = std::make_shared<streamcam::AudioFrame>();
        frame->sampleRate = 48000;
        frame->channels = 1;
        frame->frames = 960;
        frame->timestampUs = timestampUs();
        frame->pcm.resize(frame->frames);
        for (std::size_t i = 0; i < frame->pcm.size(); ++i) {
            const auto sample = static_cast<double>(audioFrames * frame->frames + i);
            const auto phase = sample * 440.0 * 6.283185307179586 / 48000.0;
            frame->pcm[i] = static_cast<std::int16_t>(std::sin(phase) * 6000.0);
        }
        if (audio.publish(frame)) ++audioDelivered;
        ++audioFrames;
    });

    videoTimer.start();
    audioTimer.start();
    QTimer::singleShot(8000, &app, [&] {
        videoTimer.stop();
        audioTimer.stop();
        std::cout << "Published synthetic OBS input: " << videoFrames << " video frames, "
                  << audioFrames << " audio packets; delivered " << videoDelivered << "/" << videoFrames
                  << " video and " << audioDelivered << "/" << audioFrames << " audio; connected clients "
                  << video.clientCount() << "/" << audio.clientCount() << ".\n";
        app.exit(videoDelivered > 0 && audioDelivered > 0 ? 0 : 5);
    });
    return app.exec();
}
