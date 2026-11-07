#include "PairingSession.h"
#include "../media/WebRtcReceiver.h"
#include <QCoreApplication>
#include <QNetworkInterface>
#include <cstdio>
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    PairingSession tokens;
    auto payload=tokens.create(QUrl("wss://10.77.0.1:47831/pair"),"public-fixture-PC",QByteArray(32,'x'));
    auto object=QJsonDocument::fromJson(payload).object();
    auto token=object.value("token").toString().toLatin1();
    auto nonce=object.value("nonce").toString().toLatin1();
    if(tokens.consume(QByteArray(43,'z'),nonce) || !tokens.consume(token,nonce) || tokens.consume(token,nonce))return 2;
    tokens.create(QUrl("wss://10.77.0.1:47831/pair"),"public-fixture-PC",QByteArray(32,'x'));
    tokens.cancel(); if(tokens.consume(token,nonce))return 3;
    QString address;
    for(auto candidate:QNetworkInterface::allAddresses()) {
        auto value=candidate.toIPv4Address();
        if((value>>24)==10 || (value>>20)==0xac1 || (value>>16)==0xc0a8) { address=candidate.toString(); break; }
    }
    streamcam::WebRtcReceiver receiver(nullptr,address);
    std::puts("Token rejection, replay, cancellation and receiver initialization passed");
    return 0;
}
