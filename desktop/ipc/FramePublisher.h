#pragma once
#include "../media/VideoFrame.h"
#include <QLocalServer>
#include <QLocalSocket>
#include <QSet>
#include <QtEndian>
#include <memory>
#include <QVariant>

namespace streamcam {
// Windows named pipe, restricted by Qt to the current user. No TCP listener.
class FramePublisher final : public QObject {
public:
    explicit FramePublisher(QObject *parent = nullptr) : QObject(parent) {
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
    ~FramePublisher() override {
        server_.close();
        for (auto *socket : clients_) {
            QObject::disconnect(socket, nullptr, this, nullptr);
            socket->abort();
        }
        clients_.clear();
    }
    bool start() { return server_.listen(QStringLiteral("StreamCamFrames-v1")); }
    void clear() {
        const auto clients=clients_;
        for(auto *socket:clients)socket->abort();
    }
    void publish(const std::shared_ptr<const VideoFrame> &frame) {
        if (!frame || !frame->valid()) return;
        QByteArray header(40, '\0');
        auto *data = reinterpret_cast<uchar *>(header.data());
        qToLittleEndian<quint32>(0x314d4353, data); // SCM1
        qToLittleEndian<quint32>(1, data + 4);
        qToLittleEndian<quint32>(frame->width, data + 8);
        qToLittleEndian<quint32>(frame->height, data + 12);
        qToLittleEndian<quint32>(frame->stride, data + 16);
        qToLittleEndian<quint32>(static_cast<quint32>(frame->bgra.size()), data + 20);
        qToLittleEndian<quint64>(frame->timestampUs, data + 24);
        // bytes 32..39 reserved; must be zero.
        for (auto *socket : clients_) {
            if (socket->bytesToWrite() != 0) continue; // drop newest for a slow consumer
            if (socket->property("hasFrame").toBool() && socket->property("lastFrameTimestamp").toLongLong()==frame->timestampUs) continue;
            socket->write(header);
            socket->write(reinterpret_cast<const char *>(frame->bgra.data()), static_cast<qint64>(frame->bgra.size()));
            socket->setProperty("hasFrame",true);
            socket->setProperty("lastFrameTimestamp",static_cast<qlonglong>(frame->timestampUs));
        }
    }
private:
    QLocalServer server_;
    QSet<QLocalSocket *> clients_;
};
}
