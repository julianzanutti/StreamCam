#pragma once
#include "PairingSession.h"
#include <QObject>
#include <QHostAddress>
#include <QSslConfiguration>
#include <QTimer>
#include <QWebSocket>
#include <QWebSocketServer>
#include <QSet>
#include "TrustedDevices.h"

class PairingServer final : public QObject {
    Q_OBJECT
public:
    explicit PairingServer(QObject *parent = nullptr, const QString &deviceStorePath = {});
    ~PairingServer() override;
    QByteArray start(const QHostAddress &address, const QSslConfiguration &tls,
                     const QString &pcId, const QByteArray &pin);
    void stop();
    void confirm(bool accepted);
    void revokeDevice(const QString &id) { trusted_.revoke(id); }
    QStringList trustedDeviceIds() const { return trusted_.activeIds(); }
signals:
    void confirmationRequested();
    void statusChanged(const QString &status);
    void paired(QWebSocket *socket);
private:
    QWebSocketServer server_{QStringLiteral("StreamCam"), QWebSocketServer::SecureMode};
    PairingSession session_;
    QTimer expiry_;
    QSet<QWebSocket *> clients_;
    QWebSocket *pending_ = nullptr;
    QString pcId_, pendingDeviceId_;
    QByteArray pendingDeviceKey_;
    TrustedDevices trusted_;
};
