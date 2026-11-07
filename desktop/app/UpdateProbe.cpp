#include "UpdateRelease.h"
#include <QCoreApplication>
#include <cstdio>
#include <cstdlib>

static void check(bool condition, const char *name) {
    if (!condition) { std::fprintf(stderr, "FAILED: %s\n", name); std::exit(1); }
}
static QJsonObject release(const QString &tag, bool installer = true) {
    return {{"tag_name", tag}, {"draft", false}, {"prerelease", tag.endsWith("-preview")},
        {"html_url", QStringLiteral("https://github.com/julianzanutti/StreamCam/releases/tag/") + tag},
        {"assets", QJsonArray{QJsonObject{{"name", installer ? "StreamCam-windows-x64-setup.exe" : "android.apk"}}}}};
}
static auto select(const QJsonArray &array) {
    return streamcam::newestWindowsRelease(QJsonDocument(array).toJson());
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto newer = select({release("v0.2.2-preview"), release("v0.1.0-preview")});
    check(newer && streamcam::isNewerRelease(*newer, "0.2.0-preview"), "preview update detected");
    check(!streamcam::isNewerRelease(*newer, "0.2.2-preview"), "same version ignored");
    check(!streamcam::isNewerRelease(*newer, "0.3.0-preview"), "older version ignored");
    check(!select({release("v0.3.0-preview", false)}), "Android-only release ignored");
    auto draft = release("v9.0.0-preview"); draft["draft"] = true;
    check(!select({draft}), "draft ignored");
    auto foreign = release("v9.0.0-preview"); foreign["html_url"] = "https://example.com/download";
    check(!select({foreign}), "foreign URL rejected");
    check(!streamcam::newestWindowsRelease("{bad json"), "invalid response rejected");
    check(!select({release("vbanana")}), "invalid version rejected");
    const auto stable = select({release("v0.2.2-preview"), release("v0.2.2")});
    check(stable && !stable->preview && streamcam::isNewerRelease(*stable, "0.2.2-preview"), "stable upgrade preferred");
    const auto unordered = select({release("v0.1.0-preview"), release("v0.3.0-preview"), release("v0.2.0-preview")});
    check(unordered && unordered->tag == "v0.3.0-preview", "highest version selected");
    std::puts("Update checks passed: 10 release selection and version cases");
}
