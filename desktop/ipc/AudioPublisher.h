#pragma once
#include "../media/AudioFrame.h"
#include <QLocalServer>
#include <QLocalSocket>
#include <QSet>
#include <QtEndian>

namespace streamcam {
// User-scoped Windows named pipe. Audio is transient PCM and is never written to disk.
class AudioPublisher final : public QObject {
public:
    explicit AudioPublisher(QObject *parent = nullptr) : QObject(parent) {
        server_.setSocketOptions(QLocalServer::UserAccessOption);
        connect(&server_, &QLocalServer::newConnection, this, [this] {
            while (auto *socket = server_.nextPendingConnection()) {
                if (clients_.size() >= 8) { socket->disconnectFromServer(); socket->deleteLater(); continue; }
                clients_.insert(socket);
                connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
                    clients_.remove(socket); socket->deleteLater();
                });
            }
        });
    }
    ~AudioPublisher() override {
        server_.close();
        for (auto *socket : clients_) { QObject::disconnect(socket, nullptr, this, nullptr); socket->abort(); }
        clients_.clear();
    }
    bool start() { return server_.listen(QStringLiteral("StreamCamAudio-v1")); }
    void clear() { const auto clients=clients_; for (auto *socket:clients) socket->abort(); }
    void publish(const std::shared_ptr<const AudioFrame> &audio) {
        if (!audio || !audio->valid()) return;
        const auto payloadBytes=audio->pcm.size()*static_cast<qsizetype>(sizeof(std::int16_t));
        QByteArray header(32, '\0'); auto *data=reinterpret_cast<uchar *>(header.data());
        qToLittleEndian<quint32>(0x31414353, data); // SCA1
        qToLittleEndian<quint32>(1, data+4);
        qToLittleEndian<quint32>(audio->sampleRate, data+8);
        qToLittleEndian<quint32>(audio->channels, data+12);
        qToLittleEndian<quint32>(audio->frames, data+16);
        qToLittleEndian<quint64>(audio->timestampUs, data+20);
        // bytes 28..31 reserved; must equal zero.
        const auto *pcm=reinterpret_cast<const char *>(audio->pcm.data());
        for (auto *socket : clients_) {
            if (socket->bytesToWrite() > 9600 || socket->property("lastAudioTimestamp").toULongLong()==audio->timestampUs) continue;
            socket->write(header); socket->write(pcm, payloadBytes);
            socket->setProperty("lastAudioTimestamp", QVariant::fromValue<qulonglong>(audio->timestampUs));
        }
    }
private:
    QLocalServer server_;
    QSet<QLocalSocket *> clients_;
};
}
