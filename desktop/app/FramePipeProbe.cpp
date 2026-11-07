#include "../ipc/FramePublisher.h"
#include <QCoreApplication>
#include <QTimer>
#include <QLocalSocket>
#include <cstdio>
#include <windows.h>

int main(int argc, char **argv) {
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS *exception) -> LONG {
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        std::fprintf(stderr, "Pipe exception offset=%llx\n", static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(exception->ExceptionRecord->ExceptionAddress) - base));
        void *stack[20]; const auto count = CaptureStackBackTrace(0, 20, stack, nullptr);
        for (unsigned i=0; i<count; ++i) std::fprintf(stderr, "stack offset %llx\n", static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(stack[i])-base));
        std::fflush(stderr); return EXCEPTION_EXECUTE_HANDLER;
    });
    QCoreApplication app(argc, argv);
    streamcam::FramePublisher publisher;
    if (!publisher.start()) { std::fprintf(stderr, "Pipe busy: close StreamCam before running probe\n"); return 2; }
    auto frame = std::make_shared<streamcam::VideoFrame>();
    frame->width = 64; frame->height = 48; frame->stride = 256; frame->timestampUs = 123456;
    frame->bgra.resize(frame->stride * frame->height);
    for (std::size_t i = 0; i < frame->bgra.size(); i += 4) {
        frame->bgra[i] = 12; frame->bgra[i+1] = 34; frame->bgra[i+2] = 56; frame->bgra[i+3] = 255;
    }
    QLocalSocket reader;
    QByteArray received;
    QObject::connect(&reader, &QLocalSocket::readyRead, &app, [&] {
        received += reader.readAll();
        if (received.size() < 40 + static_cast<qsizetype>(frame->bgra.size())) return;
        auto *header = reinterpret_cast<const uchar *>(received.constData());
        if (qFromLittleEndian<quint32>(header) != 0x314d4353 || qFromLittleEndian<quint32>(header+4) != 1 ||
            qFromLittleEndian<quint32>(header+8) != 64 || qFromLittleEndian<quint32>(header+12) != 48 ||
            qFromLittleEndian<quint32>(header+16) != 256 || qFromLittleEndian<quint32>(header+20) != frame->bgra.size() ||
            qFromLittleEndian<quint64>(header+24) != 123456 || qFromLittleEndian<quint64>(header+32) != 0 ||
            received.mid(40) != QByteArray(reinterpret_cast<const char *>(frame->bgra.data()), frame->bgra.size())) {
            app.exit(3); return;
        }
        std::puts("Frame pipe passed: exact header, timestamp and BGRA payload"); app.exit(0);
    });
    reader.connectToServer(QStringLiteral("StreamCamFrames-v1"), QIODevice::ReadOnly);
    QTimer writer;
    QObject::connect(&writer, &QTimer::timeout, &app, [&] { publisher.publish(frame); });
    writer.start(100);
    QTimer::singleShot(5000, &app, [&] { app.exit(4); });
    return app.exec();
}
