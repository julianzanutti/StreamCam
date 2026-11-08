#include "PairingServer.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QRandomGenerator>
#include <cstdio>
#include <QSslCertificate>
#include <QSslKey>
#include <stdexcept>
#include "DeviceProof.h"

PairingServer::PairingServer(QObject *parent, const QString &deviceStorePath) : QObject(parent), trusted_(deviceStorePath) {
    expiry_.setSingleShot(true);
    connect(&expiry_, &QTimer::timeout, this, [this] {
        const bool usb=server_.serverAddress().isLoopback();
        stop();emit statusChanged(usb ? QStringLiteral("La conexión USB venció. Pulsá Conectar por USB.") : QStringLiteral("El QR venció. Generá uno nuevo."));
    });
    connect(&server_, &QWebSocketServer::newConnection, this, [this] {
        auto *socket = server_.nextPendingConnection();
        if (!socket) return;
#ifdef STREAMCAM_AUTH_PROBE
        if(qEnvironmentVariableIsSet("STREAMCAM_AUTH_REPORT_USB_CODE")) { std::puts("{\"type\":\"probe.usb_socket_connected\"}"); std::fflush(stdout); }
#endif
        if (server_.serverAddress()==QHostAddress(QHostAddress::LocalHost) && usbVerificationCode_.isEmpty()) {
            socket->close(QWebSocketProtocol::CloseCodePolicyViolated); socket->deleteLater(); return;
        }
        if (clients_.size() >= 4 || socket->requestUrl().path() != QStringLiteral("/pair")) {
            socket->close(QWebSocketProtocol::CloseCodePolicyViolated); socket->deleteLater(); return;
        }
        clients_.insert(socket);
        socket->setMaxAllowedIncomingMessageSize(4096);
        socket->setMaxAllowedIncomingFrameSize(4096);
        QPointer<QWebSocket> guard(socket);
        QTimer::singleShot(10000, this, [guard, this] {
            if (guard && pending_ != guard && !guard->property("paired").toBool() &&
                !guard->property("usbChallengeActive").toBool())
                guard->close(QWebSocketProtocol::CloseCodeGoingAway);
        });
        connect(socket, &QWebSocket::disconnected, this, [this, socket] {
            clients_.remove(socket);
            if (pending_ == socket) {
                invalidatePending();
                expiry_.stop(); session_.cancel(); server_.close();
                const auto otherClients = clients_;
                clients_.clear();
                for (auto *other : otherClients) {
                    if (other == socket) continue;
                    QObject::disconnect(other, nullptr, this, nullptr);
                    other->close(QWebSocketProtocol::CloseCodeGoingAway);
                    other->deleteLater();
                }
                emit statusChanged(QStringLiteral("El teléfono se desconectó antes de confirmar. Iniciá una conexión nueva."));
            }
            socket->deleteLater();
        });
        connect(socket, &QWebSocket::textMessageReceived, this, [this, socket](const QString &text) {
#ifdef STREAMCAM_AUTH_PROBE
            if(qEnvironmentVariableIsSet("STREAMCAM_AUTH_REPORT_USB_CODE")) { std::puts("{\"type\":\"probe.usb_message_received\"}"); std::fflush(stdout); }
#endif
            auto reject = [socket] {
                socket->sendTextMessage(QStringLiteral("{\"type\":\"pair.rejected\"}"));
                socket->close(QWebSocketProtocol::CloseCodePolicyViolated);
            };
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(text.toUtf8(), &error);
            const auto object = document.object();
            const auto messageType=object.value(QStringLiteral("type")).toString();
            const bool usb=server_.serverAddress()==QHostAddress(QHostAddress::LocalHost);
#ifdef STREAMCAM_AUTH_PROBE
            if(qEnvironmentVariableIsSet("STREAMCAM_AUTH_REPORT_USB_CODE")) {
                const auto diagnostic=QJsonDocument(QJsonObject{{"type","probe.usb_request"},{"is_loopback",usb},
                    {"version",object.value(QStringLiteral("version"))},{"message_type",messageType},
                    {"field_count",object.size()},{"pending",pending_!=nullptr},{"has_code",!usbVerificationCode_.isEmpty()}})
                    .toJson(QJsonDocument::Compact);
                std::puts(diagnostic.constData()); std::fflush(stdout);
            }
#endif
            if(usb && error.error==QJsonParseError::NoError && object.value(QStringLiteral("version"))==QJsonValue(1) &&
                messageType==QStringLiteral("usb.hello") && object.size()==4 && !pending_) {
                if(!socket->property("usbChallenge").isNull()) {reject();return;}
                const auto key=QByteArray::fromBase64(object.value(QStringLiteral("device_key")).toString().toLatin1());
                const auto id=QString::fromLatin1(QCryptographicHash::hash(key,QCryptographicHash::Sha256).toHex());
                try {if(key.size()<256 || key.size()>1024 || id!=object.value(QStringLiteral("device_id")).toString() || trusted_.revoked(id)) {reject();return;}}
                catch(...) {reject();return;}
                QByteArray challenge(32,'\0'); for(auto &byte:challenge)byte=static_cast<char>(QRandomGenerator::system()->generate()&255);
                challenge=challenge.toBase64(QByteArray::Base64UrlEncoding|QByteArray::OmitTrailingEquals);
                if(usbVerificationCode_.isEmpty()) { reject(); return; }
                socket->setProperty("usbChallenge",challenge);socket->setProperty("usbId",id);socket->setProperty("usbKey",key);
                socket->setProperty("usbChallengeActive",true);
                QTimer::singleShot(60000, this, [guard=QPointer<QWebSocket>(socket),this] {
                    if(guard && guard->property("usbChallengeActive").toBool() && pending_!=guard &&
                        !guard->property("paired").toBool()) {
                        guard->setProperty("usbChallengeActive",false);
                        guard->close(QWebSocketProtocol::CloseCodeGoingAway);
                    }
                });
                const auto challengeMessage=QString::fromUtf8(QJsonDocument(QJsonObject{
                    {QStringLiteral("type"),QStringLiteral("usb.challenge")},{QStringLiteral("challenge"),QString::fromLatin1(challenge)},
                    {QStringLiteral("verification_code"),usbVerificationCode_}}).toJson(QJsonDocument::Compact));
                const auto bytesQueued=socket->sendTextMessage(challengeMessage);
#ifdef STREAMCAM_AUTH_PROBE
                if(qEnvironmentVariableIsSet("STREAMCAM_AUTH_REPORT_USB_CODE")) {
                    const auto diagnostic=QJsonDocument(QJsonObject{{"type","probe.usb_challenge_sent"},{"bytes_queued",static_cast<double>(bytesQueued)}}).toJson(QJsonDocument::Compact);
                    std::puts(diagnostic.constData()); std::fflush(stdout);
                }
#endif
                return;
            }
            if(usb && error.error==QJsonParseError::NoError && object.value(QStringLiteral("version"))==QJsonValue(1) &&
                messageType==QStringLiteral("usb.proof") && object.size()==4 && !pending_) {
                socket->setProperty("usbChallengeActive",false);
                const auto challenge=socket->property("usbChallenge").toByteArray();socket->setProperty("usbChallenge",QByteArray{});
                const auto key=socket->property("usbKey").toByteArray();const auto id=socket->property("usbId").toString();
                socket->setProperty("usbKey",QByteArray{});socket->setProperty("usbId",QString{});
                const auto proof=QByteArray("StreamCam USB v1\n")+pcId_.toUtf8()+"\n"+challenge;
                if(challenge.isEmpty() || id!=object.value(QStringLiteral("device_id")).toString() ||
                    !verifyDeviceProof(key,proof,QByteArray::fromBase64(object.value(QStringLiteral("device_proof")).toString().toLatin1()))) {reject();return;}
                session_.cancel(); requestConfirmation(socket,id,key,false);
                socket->sendTextMessage(QStringLiteral("{\"type\":\"pair.pending\"}"));
                return;
            }
            if(error.error==QJsonParseError::NoError && object.value(QStringLiteral("version"))==QJsonValue(1) &&
                messageType==QStringLiteral("reconnect.hello") && object.size()==3 && !pending_) {
                const auto id=object.value(QStringLiteral("device_id")).toString();
                QByteArray key;
                try { key=trusted_.key(id); } catch(...) { reject(); return; }
                if(key.isEmpty() || !socket->property("reconnectChallenge").isNull()) { reject(); return; }
                QByteArray challenge(32,'\0'); for(auto &byte:challenge)byte=static_cast<char>(QRandomGenerator::system()->generate()&255);
                challenge=challenge.toBase64(QByteArray::Base64UrlEncoding|QByteArray::OmitTrailingEquals);
                socket->setProperty("reconnectChallenge",challenge); socket->setProperty("reconnectId",id);
                socket->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{
                    {QStringLiteral("type"),QStringLiteral("reconnect.challenge")},
                    {QStringLiteral("challenge"),QString::fromLatin1(challenge)}}).toJson(QJsonDocument::Compact)));
                return;
            }
            if(error.error==QJsonParseError::NoError && object.value(QStringLiteral("version"))==QJsonValue(1) &&
                messageType==QStringLiteral("reconnect.proof") && object.size()==4 && !pending_) {
                const auto id=socket->property("reconnectId").toString();
                const auto challenge=socket->property("reconnectChallenge").toByteArray();
                socket->setProperty("reconnectChallenge",QByteArray{});
                socket->setProperty("reconnectId",QString{});
                QByteArray key; try { key=trusted_.key(id); } catch(...) { reject(); return; }
                const auto context=QByteArray("StreamCam reconnect v1\n")+pcId_.toUtf8()+"\n"+challenge;
                if(challenge.isEmpty() || id!=object.value(QStringLiteral("device_id")).toString() ||
                    !verifyDeviceProof(key,context,QByteArray::fromBase64(object.value(QStringLiteral("device_proof")).toString().toLatin1()))) { reject(); return; }
                requestConfirmation(socket,id,key,true);
                session_.cancel();
                socket->sendTextMessage(QStringLiteral("{\"type\":\"pair.pending\"}"));
                return;
            }
            if (error.error != QJsonParseError::NoError || !document.isObject() || object.size()!=7 ||
                object.value(QStringLiteral("version")) != QJsonValue(1) ||
                object.value(QStringLiteral("type")).toString() != QStringLiteral("pair.request") ||
                !object.value(QStringLiteral("token")).isString() || !object.value(QStringLiteral("nonce")).isString() ||
                pending_) {
                reject(); return;
            }
            QString deviceId; QByteArray deviceKey;
            if(object.size()==7) {
                const auto key=QByteArray::fromBase64(object.value(QStringLiteral("device_key")).toString().toLatin1());
                const auto id=QString::fromLatin1(QCryptographicHash::hash(key,QCryptographicHash::Sha256).toHex());
                const auto proof=QByteArray("StreamCam pair v1\n")+pcId_.toUtf8()+"\n"+object.value(QStringLiteral("nonce")).toString().toUtf8();
                try {
                    if(id!=object.value(QStringLiteral("device_id")).toString() || trusted_.revoked(id) ||
                        !verifyDeviceProof(key,proof,QByteArray::fromBase64(object.value(QStringLiteral("device_proof")).toString().toLatin1()))) { reject(); return; }
                } catch(...) { reject(); return; }
                deviceId=id; deviceKey=key;
            }
            if(!session_.consume(object.value(QStringLiteral("token")).toString().toLatin1(),object.value(QStringLiteral("nonce")).toString().toLatin1())) { reject(); return; }
            requestConfirmation(socket,deviceId,deviceKey,false);
            socket->sendTextMessage(QStringLiteral("{\"type\":\"pair.pending\"}"));
        });
    });
}

PairingServer::~PairingServer() {
    expiry_.stop(); server_.close(); session_.cancel();
    for (auto *socket : clients_) {
        QObject::disconnect(socket, nullptr, this, nullptr);
        socket->abort();
    }
    clients_.clear(); pending_ = nullptr;
}

QByteArray PairingServer::start(const QHostAddress &address, const QSslConfiguration &tls,
                              const QString &pcId, const QByteArray &pin) {
    stop();
    pcId_=pcId;
    usbVerificationCode_.clear();
    const auto ip = address.toIPv4Address();
    const bool privateAddress = ip == 0x7f000001 || (ip >> 24) == 10 || (ip >> 20) == 0xac1 || (ip >> 16) == 0xc0a8;
    if (!privateAddress || tls.localCertificate().isNull() || tls.privateKey().isNull())
        throw std::invalid_argument("A private IPv4 interface and TLS identity are required");
    server_.setSslConfiguration(tls);
    server_.setMaxPendingConnections(4);
    if (!server_.listen(address, 47831)) throw std::runtime_error("Cannot listen on selected interface port 47831");
    if(address==QHostAddress(QHostAddress::LocalHost))
        usbVerificationCode_=QStringLiteral("%1").arg(QRandomGenerator::system()->bounded(1000000),6,10,QChar('0'));
    QUrl endpoint;
    endpoint.setScheme(QStringLiteral("wss")); endpoint.setHost(address.toString());
    endpoint.setPort(server_.serverPort()); endpoint.setPath(QStringLiteral("/pair"));
    expiry_.start(120000);
    return session_.create(endpoint, pcId, pin);
}

void PairingServer::requestConfirmation(QWebSocket *socket, const QString &deviceId, const QByteArray &key, bool reconnect) {
    invalidatePending();
    pending_ = socket;
    pendingDeviceId_ = deviceId;
    pendingDeviceKey_ = key;
    pendingReconnect_ = reconnect;
    if (++nextAttemptId_ == 0) ++nextAttemptId_;
    pendingAttemptId_ = nextAttemptId_;
    const auto attempt = pendingAttemptId_;
    QTimer::singleShot(0, this, [this, attempt] {
        if (pending_ && pendingAttemptId_ == attempt)
            emit confirmationRequested(attempt, pendingDeviceId_, pendingReconnect_);
    });
}

void PairingServer::invalidatePending() {
    const auto oldAttempt = pendingAttemptId_;
    pendingAttemptId_ = 0;
    pending_ = nullptr;
    pendingDeviceId_.clear();
    pendingDeviceKey_.fill('\0'); pendingDeviceKey_.clear();
    pendingReconnect_ = false;
    if (oldAttempt) emit confirmationInvalidated(oldAttempt);
}

bool PairingServer::confirm(quint64 attemptId, bool accepted) {
    if (!attemptId || pendingAttemptId_ != attemptId || !pending_ || pending_->state() != QAbstractSocket::ConnectedState) return false;
    auto *socket = pending_;
    const auto deviceId = pendingDeviceId_;
    auto deviceKey = pendingDeviceKey_;
    invalidatePending();
    if (!accepted) {
        socket->sendTextMessage(QStringLiteral("{\"type\":\"pair.rejected\"}"));
        stop(); return true;
    }
    if(!deviceId.isEmpty()) {
        try { trusted_.remember(deviceId,deviceKey); }
        catch(...) { deviceKey.fill('\0'); socket->close(); stop(); emit statusChanged(QStringLiteral("No se pudo guardar la identidad del teléfono")); return true; }
        socket->setProperty("deviceId",deviceId);
    }
    deviceKey.fill('\0');
    expiry_.stop(); server_.close(); session_.cancel();
    socket->setProperty("paired", true);
    const auto otherClients = clients_;
    for (auto *other : otherClients) {
        if (other == socket) continue;
        clients_.remove(other);
        QObject::disconnect(other, nullptr, this, nullptr);
        other->close(QWebSocketProtocol::CloseCodeGoingAway);
        other->deleteLater();
    }
    clients_.remove(socket);
    // Ownership and signaling handlers transfer to the authenticated media session.
    disconnect(socket, &QWebSocket::textMessageReceived, this, nullptr);
#ifdef STREAMCAM_AUTH_PROBE
    const bool probeMode=probeMode_, probeControlV2Enabled=probeControlV2Enabled_;
    probeMode_=false;probeControlV2Enabled_=false;
    if(probeMode) {
        QPointer<QWebSocket> guard(socket);
        connect(socket,&QWebSocket::textMessageReceived,this,[this,guard,probeControlV2Enabled](const QString &text) {
            if(!guard)return;
            const auto document=QJsonDocument::fromJson(text.toUtf8());
            if(!document.isObject()) {guard->close();return;}
            const auto object=document.object();const auto type=object.value(QStringLiteral("type")).toString();
            if(type==QStringLiteral("control.hello")) {
                if(!probeControlV2Enabled) {
                    guard->close();QTimer::singleShot(100,QCoreApplication::instance(),&QCoreApplication::quit);return;
                }
                if(object.size()!=3 || object.value(QStringLiteral("version")).toInt()!=2 ||
                    !object.value(QStringLiteral("capabilities")).isArray()) {guard->close();return;}
                const auto requested=object.value(QStringLiteral("capabilities")).toArray();
                bool ack=false;for(const auto &entry:requested)if(entry.toString()==QStringLiteral("audio.effects.ack"))ack=true;
                guard->setProperty("probeControlReady",true);guard->setProperty("probeControlAck",ack);
                guard->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{
                    {QStringLiteral("type"),QStringLiteral("control.ready")},{QStringLiteral("version"),2},
                    {QStringLiteral("capabilities"),ack?QJsonArray{QStringLiteral("audio.effects.ack")}:QJsonArray{}}
                }).toJson(QJsonDocument::Compact)));
            } else if(type==QStringLiteral("audio.effects") && guard->property("probeControlReady").toBool() &&
                guard->property("probeControlAck").toBool() && object.size()==4 && object.value(QStringLiteral("version")).toInt()==2) {
                guard->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{
                    {QStringLiteral("type"),QStringLiteral("audio.effects.ack")},{QStringLiteral("version"),2},
                    {QStringLiteral("request_id"),object.value(QStringLiteral("request_id"))},{QStringLiteral("ok"),true}
                }).toJson(QJsonDocument::Compact)));
            } else if(type==QStringLiteral("audio.effects")) {
                guard->close();QTimer::singleShot(100,QCoreApplication::instance(),&QCoreApplication::quit);
            } else if(type==QStringLiteral("audio.effects.ack") || type==QStringLiteral("control.ready")) guard->close();
        });
        const QJsonObject accepted{{QStringLiteral("type"),QStringLiteral("pair.accepted")}};
        guard->sendTextMessage(QString::fromUtf8(QJsonDocument(accepted).toJson(QJsonDocument::Compact)));
        emit paired(socket);
        return true;
    }
#endif
    QJsonObject acceptance{{QStringLiteral("type"),QStringLiteral("pair.accepted")}};
    socket->sendTextMessage(QString::fromUtf8(QJsonDocument(acceptance).toJson(QJsonDocument::Compact)));
    emit paired(socket);
    return true;
}

void PairingServer::stop() {
    expiry_.stop(); session_.cancel(); server_.close(); invalidatePending(); usbVerificationCode_.clear();
    const auto clients = clients_;
    clients_.clear();
    for (auto *socket : clients) socket->close(QWebSocketProtocol::CloseCodeGoingAway);
}
