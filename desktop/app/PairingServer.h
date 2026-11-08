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
    bool confirm(quint64 attemptId, bool accepted);
#ifdef STREAMCAM_AUTH_PROBE
    void acceptForProbe(bool controlV2Enabled) { probeMode_ = true; probeControlV2Enabled_ = controlV2Enabled; }
#endif
    void revokeDevice(const QString &id) { trusted_.revoke(id); }
    QStringList trustedDeviceIds() const { return trusted_.activeIds(); }
    QString usbVerificationCode() const { return usbVerificationCode_; }
signals:
    void confirmationRequested(quint64 attemptId, const QString &deviceId, bool reconnect);
    void confirmationInvalidated(quint64 attemptId);
    void statusChanged(const QString &status);
    void paired(QWebSocket *socket);
private:
    QWebSocketServer server_{QStringLiteral("StreamCam"), QWebSocketServer::SecureMode};
    PairingSession session_;
    QTimer expiry_;
    QSet<QWebSocket *> clients_;
    QWebSocket *pending_ = nullptr;
    QString pcId_, pendingDeviceId_;
    QString usbVerificationCode_;
    QByteArray pendingDeviceKey_;
    quint64 nextAttemptId_ = 0, pendingAttemptId_ = 0;
    bool pendingReconnect_ = false;
#ifdef STREAMCAM_AUTH_PROBE
    bool probeMode_ = false, probeControlV2Enabled_ = false;
#endif
    void requestConfirmation(QWebSocket *socket, const QString &deviceId, const QByteArray &key, bool reconnect);
    void invalidatePending();
    TrustedDevices trusted_;
};
