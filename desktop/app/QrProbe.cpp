#include "QrImage.h"
#include <QCoreApplication>
#include <cstdio>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    // Public fixture, never a real invitation or credential.
    const QByteArray fixture = R"({"version":1,"endpoint":"wss://10.77.0.1:12345/pair","pc_id":"fixture-not-real-pc","pc_key_sha256":"0000000000000000000000000000000000000000000000000000000000000000","token":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA","expires_at":"2000-01-01T00:00:00Z","nonce":"AAAAAAAAAAAAAAAAAAAAAA"})";
    if (argc != 2 || !pairingQrImage(fixture).save(QString::fromLocal8Bit(argv[1]))) return 2;
    std::puts("Public expired QR fixture rendered");
    return 0;
}
