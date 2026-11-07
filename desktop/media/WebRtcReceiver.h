#pragma once
#include "LatestFrame.h"
#include "LatestAudio.h"
#include <QObject>
#include <QJsonObject>
#include <memory>

namespace streamcam {
// All public calls on the Qt owner thread; libwebrtc owns its networking/signaling threads.
class WebRtcReceiver final : public QObject {
    Q_OBJECT
public:
    explicit WebRtcReceiver(QObject *parent = nullptr, const QString &localAddress = {});
    ~WebRtcReceiver() override;
    void receive(const QJsonObject &message);
    void receiveUsb(const QByteArray &packet);
    void reset(const QString &localAddress);
    void stop();
    std::shared_ptr<const VideoFrame> latestFrame() const;
    std::shared_ptr<const AudioFrame> latestAudio() const;
    std::uint64_t receivedFrameCount() const;
signals:
    void signalReady(const QJsonObject &message);
    void statusChanged(const QString &message);
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
