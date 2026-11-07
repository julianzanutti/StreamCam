#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QRandomGenerator>
#include <QUrl>
#include <stdexcept>

// Own only in memory. Never log the invitation or token.
class PairingSession final {
public:
    QByteArray create(const QUrl &endpoint, const QString &pcId, const QByteArray &publicKeySha256) {
        if (endpoint.scheme() != QStringLiteral("wss") || endpoint.port() <= 0 ||
            endpoint.path() != QStringLiteral("/pair") || !endpoint.userInfo().isEmpty() ||
            endpoint.hasQuery() || endpoint.hasFragment() || publicKeySha256.size() != 32)
            throw std::invalid_argument("Invalid pairing endpoint or public key pin");
        QMutexLocker lock(&mutex_);
        token_ = randomBytes(32).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
        nonce_ = randomBytes(16).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
        deadline_ = QDateTime::currentDateTimeUtc().addSecs(120);
        return QJsonDocument(QJsonObject{
            {QStringLiteral("version"), 1},
            {QStringLiteral("endpoint"), endpoint.toString(QUrl::FullyEncoded)},
            {QStringLiteral("pc_id"), pcId},
            {QStringLiteral("pc_key_sha256"), QString::fromLatin1(publicKeySha256.toHex())},
            {QStringLiteral("token"), QString::fromLatin1(token_)},
            {QStringLiteral("nonce"), QString::fromLatin1(nonce_)},
            {QStringLiteral("expires_at"), deadline_.toString(Qt::ISODateWithMs)}
        }).toJson(QJsonDocument::Compact);
    }

    // Caller must complete TLS authentication before invoking this method.
    bool consume(const QByteArray &token, const QByteArray &nonce) {
        QMutexLocker lock(&mutex_);
        if (token_.isEmpty() || QDateTime::currentDateTimeUtc() >= deadline_ ||
            !constantEqual(token_, token) || !constantEqual(nonce_, nonce)) return false;
        clear();
        return true;
    }

    void cancel() { QMutexLocker lock(&mutex_); clear(); }

private:
    static QByteArray randomBytes(int size) {
        QByteArray bytes(size, '\0');
        auto *rng = QRandomGenerator::system();
        for (int i = 0; i < size; ++i) bytes[i] = static_cast<char>(rng->generate() & 0xff);
        return bytes;
    }
    static bool constantEqual(const QByteArray &left, const QByteArray &right) {
        if (left.size() != right.size()) return false;
        unsigned int difference = 0;
        for (int i = 0; i < left.size(); ++i)
            difference |= static_cast<unsigned char>(left[i]) ^ static_cast<unsigned char>(right[i]);
        return difference == 0;
    }
    void clear() {
        token_.fill('\0'); token_.clear(); nonce_.fill('\0'); nonce_.clear(); deadline_ = {};
    }
    QMutex mutex_;
    QByteArray token_, nonce_;
    QDateTime deadline_;
};
