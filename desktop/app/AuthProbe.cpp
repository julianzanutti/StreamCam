#include "PairingServer.h"
#include "PcIdentity.h"
#include <QCoreApplication>
#include <QNetworkInterface>
#include <QTemporaryDir>
#include <cstdio>
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    app.setOrganizationName("StreamCam"); app.setApplicationName("StreamCam");
    QTemporaryDir directory;
    PairingServer server(nullptr,directory.path()+"/devices.json");
    const auto identity=PcIdentity::load(app.applicationDirPath()+"/create-identity.ps1");
    QHostAddress address;
    for(auto candidate:QNetworkInterface::allAddresses()) {
        auto value=candidate.toIPv4Address();
        if((value>>24)==10 || (value>>20)==0xac1 || (value>>16)==0xc0a8) { address=candidate; break; }
    }
    if(qEnvironmentVariableIsSet("STREAMCAM_AUTH_USB"))address=QHostAddress::LocalHost;
    auto invite=[&] {
        auto payload=server.start(address,identity.tls,identity.id,identity.pin);
        // Secret invitation goes ONLY to the parent's anonymous pipe, never to logs/files.
        auto message=QJsonDocument(QJsonObject{{"type","probe.invitation"},{"payload",QJsonDocument::fromJson(payload).object()}}).toJson(QJsonDocument::Compact);
        std::puts(message.constData()); std::fflush(stdout); payload.fill('\0');
    };
    unsigned accepted=0;
    QObject::connect(&server,&PairingServer::confirmationRequested,&app,[&] {
        const int delay=accepted==0 ? qBound(0,qEnvironmentVariableIntValue("STREAMCAM_AUTH_DELAY_MS"),28000) : 0;
        QTimer::singleShot(delay,&app,[&] { server.confirm(true); });
    });
    QObject::connect(&server,&PairingServer::paired,&app,[&](QWebSocket *socket) {
        if(++accepted==2)server.revokeDevice(socket->property("deviceId").toString());
        socket->close();
        QTimer::singleShot(200,&app,invite);
    });
    QTimer::singleShot(60000,&app,[&] { app.exit(4); });
    invite(); return app.exec();
}
