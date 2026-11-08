#include "TrustedDevices.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <cstdio>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    const auto path = directory.path() + QStringLiteral("/devices.json");
    TrustedDevices devices(path);
    const QByteArray key("synthetic-key");
    for (int i = 0; i < 64; ++i) devices.remember(QStringLiteral("active-%1").arg(i), key);
    if (devices.activeIds().size() != 64) return 2;
    for (int i = 0; i < 40; ++i) devices.revoke(QStringLiteral("active-%1").arg(i));
    if (devices.activeIds().size() != 24) return 3;
    for (int i = 0; i < 40; ++i) devices.remember(QStringLiteral("replacement-%1").arg(i), key);
    if (devices.activeIds().size() != 64) return 4;
    for (int i = 0; i < 40; ++i) {
        const auto id = QStringLiteral("active-%1").arg(i);
        if (!devices.revoked(id) || !devices.key(id).isEmpty()) return 5;
        try { devices.remember(id, key); return 6; } catch (const std::exception &) {}
    }
    try { devices.remember(QStringLiteral("over-limit"), key); return 7; } catch (const std::exception &) {}
    std::puts("TrustedDevices: active quota excludes tombstones; revoked identities stay revoked; active cap enforced");
    return 0;
}
