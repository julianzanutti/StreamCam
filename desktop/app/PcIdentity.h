#pragma once
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QCoreApplication>
#include <QFileInfo>
#include <stdexcept>

struct PcIdentity {
    QSslConfiguration tls;
    QByteArray pin;
    QString id;
    static PcIdentity load(const QString &scriptPath) {
        auto shell = QCoreApplication::applicationDirPath()+QStringLiteral("/runtime/powershell/pwsh.exe");
        if (!QFileInfo::exists(shell)) shell=QStandardPaths::findExecutable(QStringLiteral("pwsh"));
        if (shell.isEmpty()) {
            const auto installed=qEnvironmentVariable("ProgramFiles")+QStringLiteral("/PowerShell/7/pwsh.exe");
            if(QFileInfo::exists(installed))shell=installed;
        }
        if (shell.isEmpty()) throw std::runtime_error("Falta el runtime de identidad. Volvé a extraer el paquete completo de StreamCam.");
        QProcess process;
        process.start(shell, {QStringLiteral("-NoProfile"), QStringLiteral("-File"), scriptPath,
            QStringLiteral("-Directory"), QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)});
        if (!process.waitForFinished(30000) || process.exitCode() != 0)
            throw std::runtime_error("Cannot provision protected PC identity");
        auto output = process.readAllStandardOutput();
        auto document = QJsonDocument::fromJson(output);
        output.fill('\0');
        const auto object = document.object();
        QSslCertificate cert(object.value(QStringLiteral("certificate")).toString().toUtf8());
        auto der = QByteArray::fromBase64(object.value(QStringLiteral("key")).toString().toLatin1());
        const auto pem = QByteArray("-----BEGIN PRIVATE KEY-----\n") + der.toBase64() + "\n-----END PRIVATE KEY-----\n";
        QSslKey key(pem, QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey);
        der.fill('\0');
        const auto pin = QByteArray::fromHex(object.value(QStringLiteral("pin")).toString().toLatin1());
        if (cert.isNull() || key.isNull() || pin.size() != 32) throw std::runtime_error("Invalid PC identity");
        auto tls = QSslConfiguration::defaultConfiguration();
        tls.setLocalCertificate(cert); tls.setPrivateKey(key);
        tls.setProtocol(QSsl::TlsV1_2OrLater); tls.setPeerVerifyMode(QSslSocket::VerifyNone);
        return {tls, pin, QString::fromLatin1(pin.toHex().left(32))};
    }
};
