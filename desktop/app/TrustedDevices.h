#pragma once
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <stdexcept>

class TrustedDevices final {
    QString override_;
    QString path() const { return override_.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+QStringLiteral("/devices.json") : override_; }
    QJsonObject read() const {
        QFile file(path());
        if (!file.exists()) return {};
        if (!file.open(QIODevice::ReadOnly) || file.size()>65536) throw std::runtime_error("Cannot read trusted devices");
        QJsonParseError error; auto document=QJsonDocument::fromJson(file.readAll(),&error);
        if(error.error!=QJsonParseError::NoError || !document.isObject()) throw std::runtime_error("Invalid device store");
        return document.object();
    }
    void write(const QJsonObject &devices) {
        QDir().mkpath(QFileInfo(path()).absolutePath());
        QSaveFile file(path());
        if(!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(devices).toJson(QJsonDocument::Compact))<0 || !file.commit())
            throw std::runtime_error("Cannot persist trusted devices");
    }
public:
    explicit TrustedDevices(const QString &pathOverride={}) : override_(pathOverride) {}
    QStringList activeIds() const {
        QStringList result; const auto devices=read();
        for(auto it=devices.begin();it!=devices.end();++it) if(!it.value().toObject().value(QStringLiteral("revoked")).toBool())result.append(it.key());
        return result;
    }
    bool revoked(const QString &id) const { return read().value(id).toObject().value(QStringLiteral("revoked")).toBool(); }
    QByteArray key(const QString &id) const {
        auto object=read().value(id).toObject();
        return object.value(QStringLiteral("revoked")).toBool() ? QByteArray{} : QByteArray::fromBase64(object.value(QStringLiteral("key")).toString().toLatin1());
    }
    void remember(const QString &id,const QByteArray &key) {
        auto devices=read();
        const auto existing = devices.value(id).toObject();
        if(existing.value(QStringLiteral("revoked")).toBool())throw std::runtime_error("Device revoked");
        const bool alreadyActive = devices.contains(id);
        if(!alreadyActive) {
            int activeCount = 0;
            for(auto it=devices.begin();it!=devices.end();++it)
                if(!it.value().toObject().value(QStringLiteral("revoked")).toBool()) ++activeCount;
            if(activeCount>=64)throw std::runtime_error("Device limit reached");
        }
        devices.insert(id,QJsonObject{{QStringLiteral("key"),QString::fromLatin1(key.toBase64())},{QStringLiteral("revoked"),false}});
        write(devices);
    }
    void revoke(const QString &id) {
        auto devices=read(); auto object=devices.value(id).toObject();
        object.insert(QStringLiteral("revoked"),true); devices.insert(id,object); write(devices);
    }
};
