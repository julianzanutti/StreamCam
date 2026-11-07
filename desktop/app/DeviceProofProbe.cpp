#include "DeviceProof.h"
#include "TrustedDevices.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <cstdio>
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    if(argc!=2)return 2;
    QFile fixture(QString::fromLocal8Bit(argv[1]));
    if(!fixture.open(QIODevice::ReadOnly))return 3;
    auto object=QJsonDocument::fromJson(fixture.readAll()).object();
    auto key=QByteArray::fromBase64(object.value("key").toString().toLatin1());
    auto signature=QByteArray::fromBase64(object.value("signature").toString().toLatin1());
    auto message=object.value("message").toString().toUtf8();
    if(!verifyDeviceProof(key,message,signature) || verifyDeviceProof(key,message+"tampered",signature))return 4;
    signature[0]^=1;
    if(verifyDeviceProof(key,message,signature))return 5;
    QTemporaryDir directory;
    auto path=directory.path()+"/devices.json";
    const auto id=QString::fromLatin1(QCryptographicHash::hash(key,QCryptographicHash::Sha256).toHex());
    TrustedDevices devices(path); devices.remember(id,key);
    TrustedDevices reloaded(path);
    if(reloaded.key(id)!=key)return 6;
    reloaded.revoke(id);
    TrustedDevices revoked(path);
    if(!revoked.revoked(id) || !revoked.key(id).isEmpty())return 7;
    try { revoked.remember(id,key); return 8; } catch(const std::exception &) {}
    std::puts("Device signature, tamper rejection, persistence and revocation passed");
    return 0;
}
