#pragma once
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>
#include <QVersionNumber>
#include <optional>

namespace streamcam {
struct UpdateRelease {
    QString tag;
    QVersionNumber version;
    bool preview;
    QUrl url;
};

// Preview builds are published as prereleases, so /releases/latest is insufficient.
inline std::optional<UpdateRelease> newestWindowsRelease(const QByteArray &body) {
    const auto document = QJsonDocument::fromJson(body);
    if (!document.isArray()) return std::nullopt;
    const QRegularExpression tagPattern(QStringLiteral("^v?([0-9]+\\.[0-9]+\\.[0-9]+)(-preview)?$"));
    std::optional<UpdateRelease> newest;
    for (const auto &entry : document.array()) {
        const auto release = entry.toObject();
        if (!release.contains(QStringLiteral("draft")) || release.value(QStringLiteral("draft")).toBool(true)) continue;
        const auto tag = release.value(QStringLiteral("tag_name")).toString();
        const auto match = tagPattern.match(tag);
        if (!match.hasMatch()) continue;
        const QUrl url(release.value(QStringLiteral("html_url")).toString());
        if (url.scheme() != QStringLiteral("https") || url.host() != QStringLiteral("github.com") ||
            !url.userInfo().isEmpty() || url.port() != -1 || !url.query().isEmpty() || !url.fragment().isEmpty() ||
            url.path() != QStringLiteral("/julianzanutti/StreamCam/releases/tag/") + tag) continue;
        bool hasInstaller = false;
        for (const auto &asset : release.value(QStringLiteral("assets")).toArray())
            if (asset.toObject().value(QStringLiteral("name")).toString().endsWith(QStringLiteral("-windows-x64-setup.exe")))
                hasInstaller = true;
        if (!hasInstaller) continue;
        UpdateRelease candidate{tag, QVersionNumber::fromString(match.captured(1)), !match.captured(2).isEmpty(), url};
        if (!newest || candidate.version > newest->version ||
            (candidate.version == newest->version && newest->preview && !candidate.preview)) newest = candidate;
    }
    return newest;
}

inline bool isNewerRelease(const UpdateRelease &release, const QString &current) {
    const auto currentVersion = QVersionNumber::fromString(current);
    return release.version > currentVersion ||
        (release.version == currentVersion && current.endsWith(QStringLiteral("-preview")) && !release.preview);
}
}
