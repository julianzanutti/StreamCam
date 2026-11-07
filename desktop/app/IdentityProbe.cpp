#include "PcIdentity.h"
#include <QTemporaryDir>
#include <cstdio>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) return 2;
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 3;
    // Keep this probe's generated identity separate from the user's paired PC.
    try {
        const auto first = PcIdentity::load(QString::fromLocal8Bit(argv[1]), temporary.path());
        const auto second = PcIdentity::load(QString::fromLocal8Bit(argv[1]), temporary.path());
        if (first.id.isEmpty() || first.pin != second.pin) return 4;
        std::puts("PASS: identity created and reloaded; no keys printed.");
        return 0;
    } catch (const std::exception &) {
        std::fputs("FAIL: identity creation or reload failed; no keys printed.\n", stderr);
        return 1;
    }
}
