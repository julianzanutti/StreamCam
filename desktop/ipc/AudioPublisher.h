#pragma once
#include "../media/AudioFrame.h"
#include "../media/VoiceProcessor.h"
#include <QLocalServer>
#include <QLocalSocket>
#include <QSet>
#include <QtEndian>
#include <QVariant>
#include <cmath>
#include <cstring>

namespace streamcam {
// User-scoped Windows named pipe. Audio is transient PCM and is never written to disk.
class AudioPublisher final : public QObject {
public:
    explicit AudioPublisher(QObject *parent = nullptr) : QObject(parent) {
        connect(&server_, &QLocalServer::newConnection, this, [this] { acceptPendingConnections(); });
    }
    ~AudioPublisher() override {
        server_.close();
        for (auto *socket : clients_) { QObject::disconnect(socket, nullptr, this, nullptr); socket->abort(); }
        clients_.clear();
    }
    bool start(const QString &name=QStringLiteral("StreamCamAudio-v1"),
               QLocalServer::SocketOptions options=QLocalServer::UserAccessOption) {
        server_.setSocketOptions(options);
        return server_.listen(name);
    }
    QString errorString() const { return server_.errorString(); }
    QString fullServerName() const { return server_.fullServerName(); }
    std::size_t clientCount() const { return static_cast<std::size_t>(clients_.size()); }
    void acceptPendingConnections() {
        while (auto *socket = server_.nextPendingConnection()) {
            if (clients_.size() >= 8) { socket->disconnectFromServer(); socket->deleteLater(); continue; }
            clients_.insert(socket);
            connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
                clients_.remove(socket); socket->deleteLater();
            });
        }
    }
    void setVoiceSettings(const VoiceSettings &settings) { processor_.setSettings(settings); }
    const VoiceSettings &voiceSettings() const { return processor_.settings(); }
    float outputPeakDbfs() const { return outputPeakDbfs_; }
    void clear() {
        const auto clients=clients_;
        for (auto *socket:clients) socket->abort();
        processor_.reset();
        outputPeakDbfs_=-120.0F;
    }
    std::uint64_t droppedFrames() const { return droppedFrames_; }
    bool publish(const std::shared_ptr<const AudioFrame> &audio) {
        if (!audio || !audio->valid()) return false;
        AudioFrame processed=*audio;
        if (!processor_.process(processed)) return false;
        const auto channels=processed.channels, frames=processed.frames;
        const auto timestamp=processed.timestampUs;
        std::vector<std::int16_t> converted;
        const std::int16_t *pcmData=processed.pcm.data();
        if(processed.floatingPoint) {
            converted.resize(processed.samplesFloat.size());
            std::transform(processed.samplesFloat.begin(),processed.samplesFloat.end(),converted.begin(),[](float value) {
                const auto clipped=std::clamp(value,-1.0F,1.0F);
                return static_cast<std::int16_t>(std::lround(clipped*32767.0F));
            });
            pcmData=converted.data();
        }
        const auto payloadBytes=static_cast<qsizetype>(frames)*channels*static_cast<qsizetype>(sizeof(std::int16_t));
        QByteArray header(32, '\0'); auto *data=reinterpret_cast<uchar *>(header.data());
        qToLittleEndian<quint32>(0x31414353, data); // SCA1
        qToLittleEndian<quint32>(1, data+4);
        qToLittleEndian<quint32>(processed.sampleRate, data+8);
        qToLittleEndian<quint32>(channels, data+12);
        qToLittleEndian<quint32>(frames, data+16);
        qToLittleEndian<quint64>(timestamp, data+20);
        // bytes 28..31 reserved; must equal zero.
        QByteArray packet(32+payloadBytes,'\0');
        std::memcpy(packet.data(),header.constData(),32);
        if(payloadBytes>0)std::memcpy(packet.data()+32,pcmData,static_cast<std::size_t>(payloadBytes));
        float peak=0.0F;
        for(std::size_t i=0;i<static_cast<std::size_t>(frames)*channels;++i)
            peak=std::max(peak,std::abs(static_cast<float>(pcmData[i])/32768.0F));
        const auto peakDbfs=peak>1.0e-6F ? 20.0F*std::log10(peak) : -120.0F;
        bool delivered=false;
        for (auto *socket : clients_) {
            if (socket->bytesToWrite() > 9600) { ++droppedFrames_; continue; }
            if (socket->property("lastAudioTimestamp").toULongLong()==timestamp) continue;
            if(socket->write(packet)==packet.size()) delivered=true;
            else {++droppedFrames_;socket->abort();continue;}
            socket->setProperty("lastAudioTimestamp", QVariant::fromValue<qulonglong>(timestamp));
        }
        outputPeakDbfs_=delivered?peakDbfs:-120.0F;
        return delivered;
    }
private:
    QLocalServer server_;
    QSet<QLocalSocket *> clients_;
    std::uint64_t droppedFrames_ = 0;
    float outputPeakDbfs_=-120.0F; // Read only on the worker thread.
    VoiceProcessor processor_;
};
}
