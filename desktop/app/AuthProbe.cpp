#include "PairingServer.h"
#include "PcIdentity.h"
#include <QCoreApplication>
#include <QNetworkInterface>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    app.setOrganizationName("StreamCam"); app.setApplicationName("StreamCam");
    QTemporaryDir directory;
    PairingServer server(nullptr,directory.path()+"/devices.json");
    const auto identityDirectory=qEnvironmentVariable("STREAMCAM_AUTH_IDENTITY_DIR");
    const auto identity=PcIdentity::load(app.applicationDirPath()+"/create-identity.ps1",
        identityDirectory.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::TempLocation)+"/StreamCamAuthProbe":identityDirectory);
    QHostAddress address;
    for(auto candidate:QNetworkInterface::allAddresses()) {
        auto value=candidate.toIPv4Address();
        if((value>>24)==10 || (value>>20)==0xac1 || (value>>16)==0xc0a8) { address=candidate; break; }
    }
    if(qEnvironmentVariableIsSet("STREAMCAM_AUTH_USB"))address=QHostAddress::LocalHost;
    auto invite=[&] {
        auto payload=server.start(address,identity.tls,identity.id,identity.pin);
        // Secret invitation goes ONLY to the parent's anonymous pipe, never to logs/files.
        QJsonObject envelope{{"type","probe.invitation"},{"payload",QJsonDocument::fromJson(payload).object()}};
        if(qEnvironmentVariableIsSet("STREAMCAM_AUTH_REPORT_USB_CODE"))
            envelope.insert(QStringLiteral("probe_usb_code"),server.usbVerificationCode());
        auto message=QJsonDocument(envelope).toJson(QJsonDocument::Compact);
        std::puts(message.constData()); std::fflush(stdout); payload.fill('\0');
    };
    unsigned accepted=0;
    const bool staleAttemptMode=qEnvironmentVariableIsSet("STREAMCAM_AUTH_STALE_ATTEMPT");
    const bool controlProbe=qEnvironmentVariableIsSet("STREAMCAM_AUTH_CONTROL_PROBE");
    const bool controlV2Enabled=qEnvironmentVariableIntValue("STREAMCAM_AUTH_CONTROL_V2")!=0;
    const int controlPairs=qEnvironmentVariableIntValue("STREAMCAM_AUTH_CONTROL_PAIRS")>0
        ? qEnvironmentVariableIntValue("STREAMCAM_AUTH_CONTROL_PAIRS"):1;
    if(controlProbe)qputenv("STREAMCAM_AUTH_USB","1");
    quint64 firstAttempt=0;
    bool staleResponseRejected=false;
    unsigned confirmationCount=0;
    QObject::connect(&server,&PairingServer::confirmationRequested,&app,[&](quint64 attemptId,const QString &,bool) {
        if(staleAttemptMode) {
            if(++confirmationCount==1) {
                firstAttempt=attemptId;
                QTimer::singleShot(500,&app,[&] {
                    if(!server.confirm(firstAttempt,true)) staleResponseRejected=true;
                    else app.exit(2);
                });
            } else if(confirmationCount==2) {
                const int delay=controlProbe?20:900;
                QTimer::singleShot(delay,&app,[&,attemptId] {
                    if(!server.confirm(attemptId,true)) app.exit(3);
                });
            } else app.exit(4);
            return;
        }
        const int delay=accepted==0 ? qBound(0,qEnvironmentVariableIntValue("STREAMCAM_AUTH_DELAY_MS"),28000) : 0;
        QTimer::singleShot(delay,&app,[&,attemptId] { server.confirm(attemptId,true); });
    });
    QObject::connect(&server,&PairingServer::confirmationInvalidated,&app,[&](quint64 attemptId) {
        if(staleAttemptMode && attemptId==firstAttempt) QTimer::singleShot(100,&app,invite);
    });
    QObject::connect(&server,&PairingServer::paired,&app,[&](QWebSocket *socket) {
        if(qEnvironmentVariableIsSet("STREAMCAM_AUTH_REPORT_USB_CODE")) {
            std::puts("{\"type\":\"probe.usb_accepted\"}"); std::fflush(stdout);
        }
        if(controlProbe) {
            return;
        }
        if(staleAttemptMode) {
            if(staleResponseRejected) {
                ++accepted;
                socket->close();
                std::puts("PASS: stale consent rejected while a different authenticated phone awaited approval");
                app.exit(0);
            }
            return;
        }
        if(staleAttemptMode)return;
        if(++accepted==2)server.revokeDevice(socket->property("deviceId").toString());
        socket->close();
        QTimer::singleShot(200,&app,invite);
    });
    if(controlProbe) {
        server.acceptForProbe(controlV2Enabled);
        QObject::connect(&server,&PairingServer::paired,&app,[&](QWebSocket *socket) {
            ++accepted;
            if(!controlV2Enabled)QTimer::singleShot(1200,&app,[&]{app.exit(0);});
            QObject::connect(socket,&QWebSocket::textMessageReceived,&app,[&,socket](const QString &text) {
                QJsonParseError error;const auto document=QJsonDocument::fromJson(text.toUtf8(),&error);
                if(error.error!=QJsonParseError::NoError || !document.isObject()) {socket->close();app.exit(6);return;}
                const auto object=document.object();const auto type=object.value(QStringLiteral("type")).toString();
                if(type==QStringLiteral("probe.control.close")) {
                    socket->close();
                    if(accepted<static_cast<unsigned>(controlPairs))QTimer::singleShot(100,&app,invite);else app.exit(0);
                } else if(type==QStringLiteral("control.hello") || type==QStringLiteral("control.ready") ||
                    type==QStringLiteral("audio.effects") || type==QStringLiteral("audio.effects.ack")) {
                    return; // PairingServer's loopback-only control probe owns these protocol messages.
                } else { socket->close();app.exit(7); }
            });
        });
    }
    QTimer::singleShot(60000,&app,[&] { app.exit(4); });
    invite(); return app.exec();
}
