#include "PairingServer.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
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
        if (clients_.size() >= 4 || socket->requestUrl().path() != QStringLiteral("/pair")) {
            socket->close(QWebSocketProtocol::CloseCodePolicyViolated); socket->deleteLater(); return;
        }
        clients_.insert(socket);
        socket->setMaxAllowedIncomingMessageSize(4096);
        socket->setMaxAllowedIncomingFrameSize(4096);
        QPointer<QWebSocket> guard(socket);
        QTimer::singleShot(10000, this, [guard, this] {
            if (guard && pending_ != guard && !guard->property("paired").toBool())
                guard->close(QWebSocketProtocol::CloseCodeGoingAway);
        });
        connect(socket, &QWebSocket::disconnected, this, [this, socket] {
            clients_.remove(socket);
            if (pending_ == socket) pending_ = nullptr;
            socket->deleteLater();
        });
        connect(socket, &QWebSocket::textMessageReceived, this, [this, socket](const QString &text) {
            auto reject = [socket] {
                socket->sendTextMessage(QStringLiteral("{\"type\":\"pair.rejected\"}"));
                socket->close(QWebSocketProtocol::CloseCodePolicyViolated);
            };
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(text.toUtf8(), &error);
            const auto object = document.object();
            const auto messageType=object.value(QStringLiteral("type")).toString();
            const bool usb=server_.serverAddress()==QHostAddress(QHostAddress::LocalHost);
            if(usb && error.error==QJsonParseError::NoError && object.value(QStringLiteral("version"))==QJsonValue(1) &&
                messageType==QStringLiteral("usb.hello") && object.size()==4 && !pending_) {
                if(!socket->property("usbChallenge").isNull()) {reject();return;}
                const auto key=QByteArray::fromBase64(object.value(QStringLiteral("device_key")).toString().toLatin1());
                const auto id=QString::fromLatin1(QCryptographicHash::hash(key,QCryptographicHash::Sha256).toHex());
                try {if(key.size()<256 || key.size()>1024 || id!=object.value(QStringLiteral("device_id")).toString() || trusted_.revoked(id)) {reject();return;}}
                catch(...) {reject();return;}
                QByteArray challenge(32,'\0'); for(auto &byte:challenge)byte=static_cast<char>(QRandomGenerator::system()->generate()&255);
                challenge=challenge.toBase64(QByteArray::Base64UrlEncoding|QByteArray::OmitTrailingEquals);
                socket->setProperty("usbChallenge",challenge);socket->setProperty("usbId",id);socket->setProperty("usbKey",key);
                socket->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{
                    {QStringLiteral("type"),QStringLiteral("usb.challenge")},{QStringLiteral("challenge"),QString::fromLatin1(challenge)}}).toJson(QJsonDocument::Compact)));
                return;
            }
            if(usb && error.error==QJsonParseError::NoError && object.value(QStringLiteral("version"))==QJsonValue(1) &&
                messageType==QStringLiteral("usb.proof") && object.size()==4 && !pending_) {
                const auto challenge=socket->property("usbChallenge").toByteArray();socket->setProperty("usbChallenge",QByteArray{});
                const auto key=socket->property("usbKey").toByteArray();const auto id=socket->property("usbId").toString();
                const auto proof=QByteArray("StreamCam USB v1\n")+pcId_.toUtf8()+"\n"+challenge;
                if(challenge.isEmpty() || id!=object.value(QStringLiteral("device_id")).toString() ||
                    !verifyDeviceProof(key,proof,QByteArray::fromBase64(object.value(QStringLiteral("device_proof")).toString().toLatin1()))) {reject();return;}
                pendingDeviceId_=id;pendingDeviceKey_=key;pending_=socket;session_.cancel();
                socket->sendTextMessage(QStringLiteral("{\"type\":\"pair.pending\"}"));
                QTimer::singleShot(0,this,[this] {if(pending_)emit confirmationRequested();});return;
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
                QByteArray key; try { key=trusted_.key(id); } catch(...) { reject(); return; }
                const auto context=QByteArray("StreamCam reconnect v1\n")+pcId_.toUtf8()+"\n"+challenge;
                if(challenge.isEmpty() || id!=object.value(QStringLiteral("device_id")).toString() ||
                    !verifyDeviceProof(key,context,QByteArray::fromBase64(object.value(QStringLiteral("device_proof")).toString().toLatin1()))) { reject(); return; }
                pendingDeviceId_=id; pendingDeviceKey_=key; pending_=socket;
                session_.cancel();
                socket->sendTextMessage(QStringLiteral("{\"type\":\"pair.pending\"}"));
                QTimer::singleShot(0,this,[this] { if(pending_)emit confirmationRequested(); });
                return;
            }
            if (error.error != QJsonParseError::NoError || !document.isObject() || object.size()!=7 ||
                object.value(QStringLiteral("version")) != QJsonValue(1) ||
                object.value(QStringLiteral("type")).toString() != QStringLiteral("pair.request") ||
                !object.value(QStringLiteral("token")).isString() || !object.value(QStringLiteral("nonce")).isString() ||
                pending_) {
                reject(); return;
            }
            pendingDeviceId_.clear(); pendingDeviceKey_.clear();
            if(object.size()==7) {
                const auto key=QByteArray::fromBase64(object.value(QStringLiteral("device_key")).toString().toLatin1());
                const auto id=QString::fromLatin1(QCryptographicHash::hash(key,QCryptographicHash::Sha256).toHex());
                const auto proof=QByteArray("StreamCam pair v1\n")+pcId_.toUtf8()+"\n"+object.value(QStringLiteral("nonce")).toString().toUtf8();
                try {
                    if(id!=object.value(QStringLiteral("device_id")).toString() || trusted_.revoked(id) ||
                        !verifyDeviceProof(key,proof,QByteArray::fromBase64(object.value(QStringLiteral("device_proof")).toString().toLatin1()))) { reject(); return; }
                } catch(...) { reject(); return; }
                pendingDeviceId_=id; pendingDeviceKey_=key;
            }
            if(!session_.consume(object.value(QStringLiteral("token")).toString().toLatin1(),object.value(QStringLiteral("nonce")).toString().toLatin1())) { reject(); return; }
            pending_ = socket;
            socket->sendTextMessage(QStringLiteral("{\"type\":\"pair.pending\"}"));
            QTimer::singleShot(0,this,[this] { if(pending_)emit confirmationRequested(); });
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
    const auto ip = address.toIPv4Address();
    const bool privateAddress = ip == 0x7f000001 || (ip >> 24) == 10 || (ip >> 20) == 0xac1 || (ip >> 16) == 0xc0a8;
    if (!privateAddress || tls.localCertificate().isNull() || tls.privateKey().isNull())
        throw std::invalid_argument("A private IPv4 interface and TLS identity are required");
    server_.setSslConfiguration(tls);
    server_.setMaxPendingConnections(4);
    if (!server_.listen(address, 47831)) throw std::runtime_error("Cannot listen on selected interface port 47831");
    QUrl endpoint;
    endpoint.setScheme(QStringLiteral("wss")); endpoint.setHost(address.toString());
    endpoint.setPort(server_.serverPort()); endpoint.setPath(QStringLiteral("/pair"));
    expiry_.start(120000);
    return session_.create(endpoint, pcId, pin);
}

void PairingServer::confirm(bool accepted) {
    if (!pending_) return;
    auto *socket = pending_;
    pending_ = nullptr;
    if (!accepted) {
        socket->sendTextMessage(QStringLiteral("{\"type\":\"pair.rejected\"}"));
        stop(); return;
    }
    if(!pendingDeviceId_.isEmpty()) {
        try { trusted_.remember(pendingDeviceId_,pendingDeviceKey_); }
        catch(...) { socket->close(); stop(); emit statusChanged(QStringLiteral("No se pudo guardar la identidad del teléfono")); return; }
        socket->setProperty("deviceId",pendingDeviceId_);
    }
    expiry_.stop(); server_.close(); session_.cancel();
    socket->setProperty("paired", true);
    clients_.remove(socket);
    // Ownership and signaling handlers transfer to the authenticated media session.
    disconnect(socket, &QWebSocket::textMessageReceived, this, nullptr);
    socket->sendTextMessage(QStringLiteral("{\"type\":\"pair.accepted\"}"));
    emit paired(socket);
}

void PairingServer::stop() {
    expiry_.stop(); session_.cancel(); server_.close(); pending_ = nullptr;
    const auto clients = clients_;
    clients_.clear();
    for (auto *socket : clients) socket->close(QWebSocketProtocol::CloseCodeGoingAway);
}
