#pragma once
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
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
    static PcIdentity load(const QString &scriptPath,
                           const QString &identityDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)) {
        auto shell = QCoreApplication::applicationDirPath()+QStringLiteral("/runtime/powershell/pwsh.exe");
        if (!QFileInfo::exists(shell)) shell=QStandardPaths::findExecutable(QStringLiteral("pwsh"));
        if (shell.isEmpty()) {
            const auto installed=qEnvironmentVariable("ProgramFiles")+QStringLiteral("/PowerShell/7/pwsh.exe");
            if(QFileInfo::exists(installed))shell=installed;
        }
        if (shell.isEmpty()) throw std::runtime_error("Falta el runtime de identidad. Volvé a extraer el paquete completo de StreamCam.");
        QProcess process;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("NO_COLOR"), QStringLiteral("1"));
        process.setProcessEnvironment(environment);
        if (identityDirectory.isEmpty()) throw std::runtime_error("Windows no informó una carpeta local para guardar la identidad de StreamCam.");
        process.start(shell, {QStringLiteral("-NoLogo"), QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
            QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
            QStringLiteral("-File"), scriptPath, QStringLiteral("-Directory"), identityDirectory});
        if (!process.waitForStarted(10000))
            throw std::runtime_error(QStringLiteral("No se pudo iniciar PowerShell (%1).").arg(process.errorString()).toUtf8().constData());
        if (!process.waitForFinished(30000)) {
            process.kill(); process.waitForFinished();
            throw std::runtime_error("PowerShell tardó demasiado al preparar la identidad de esta PC. Reiniciá StreamCam e intentá de nuevo.");
        }
        auto standardError = QString::fromUtf8(process.readAllStandardError());
        standardError.remove(QRegularExpression(QStringLiteral("\x1b\\[[0-9;]*m")));
        standardError = standardError.trimmed();
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            auto detail = standardError;
            if (detail.size() > 600) detail = detail.right(600);
            const auto message = detail.isEmpty()
                ? QStringLiteral("PowerShell terminó con código %1.").arg(process.exitCode())
                : QStringLiteral("PowerShell: %1").arg(detail);
            throw std::runtime_error(QStringLiteral("No se pudo preparar la identidad de esta PC. %1 Consultá la sección de identidad en SOLUCION_DE_PROBLEMAS.md.").arg(message).toUtf8().constData());
        }
        auto output = process.readAllStandardOutput();
        auto document = QJsonDocument::fromJson(output);
        output.fill('\0');
        const auto object = document.object();
        if (document.isNull() || !document.isObject())
            throw std::runtime_error("PowerShell no devolvió una identidad válida. Reextraé el ZIP completo de StreamCam y volvé a intentar.");
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
