#include "../media/WebRtcReceiver.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QTimer>
#include <QNetworkInterface>
#include <QFile>
#include <QElapsedTimer>
#include <windows.h>
#include <cstdio>
#include "../ipc/FramePublisher.h"

int main(int argc, char **argv) {
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS *exception) -> LONG {
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        std::fprintf(stderr, "Media exception offset=%llx\n", static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(exception->ExceptionRecord->ExceptionAddress)-base));
        void *stack[24]; auto count=CaptureStackBackTrace(0,24,stack,nullptr);
        for(unsigned i=0;i<count;++i) std::fprintf(stderr,"stack offset %llx\n",static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(stack[i])-base));
        std::fflush(stderr); return EXCEPTION_EXECUTE_HANDLER;
    });
    QCoreApplication app(argc, argv);
    QString address;
    for (auto ip : QNetworkInterface::allAddresses()) {
        auto value = ip.toIPv4Address();
        if ((value >> 24) == 10 || (value >> 20) == 0xac1 || (value >> 16) == 0xc0a8) { address = ip.toString(); break; }
    }
    streamcam::WebRtcReceiver receiver(nullptr, address);
    QObject::connect(&receiver, &streamcam::WebRtcReceiver::statusChanged, &app, [](const QString &message) {
        std::fprintf(stderr, "receiver status: %s\n", message.toUtf8().constData()); std::fflush(stderr);
    });
    QObject::connect(&receiver, &streamcam::WebRtcReceiver::signalReady, &app, [](const QJsonObject &message) {
        const auto line = QJsonDocument(message).toJson(QJsonDocument::Compact);
        std::puts(line.constData()); std::fflush(stdout);
    });
    QByteArray input;
    streamcam::FramePublisher publisher;
    if (!publisher.start()) return 8;
    unsigned frames=0;
    std::int64_t lastTimestamp=-1;
    QElapsedTimer elapsed;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &app, [&] {
        DWORD available = 0;
        const auto handle = GetStdHandle(STD_INPUT_HANDLE);
        if (PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr) && available) {
            QByteArray bytes(static_cast<qsizetype>(available), '\0'); DWORD read = 0;
            if (ReadFile(handle, bytes.data(), available, &read, nullptr)) input += bytes.left(read);
            while (input.contains('\n')) {
                auto end = input.indexOf('\n'); auto line = input.left(end); input.remove(0, end+1);
                std::fprintf(stderr, "probe: signaling input bytes=%lld\n", static_cast<long long>(line.size())); std::fflush(stderr);
                try { receiver.receive(QJsonDocument::fromJson(line).object()); } catch (...) { app.exit(3); }
            }
        }
        if (auto frame = receiver.latestFrame(); frame && frame->valid()) {
            publisher.publish(frame);
            if (frame->timestampUs==lastTimestamp) return;
            lastTimestamp=frame->timestampUs;
            if (frames==0) elapsed.start();
            if (++frames < 900) return;
            if (frame->bgra[0]<50 || frame->bgra[0]>100 || frame->bgra[1]<50 || frame->bgra[1]>100 ||
                frame->bgra[2]<50 || frame->bgra[2]>100 || frame->bgra[3]!=255) { app.exit(9); return; }
            std::fprintf(stderr,"Continuity: %u unique frames in %lld ms\n",frames,static_cast<long long>(elapsed.elapsed()));
            std::printf("{\"type\":\"probe.frame\",\"width\":%d,\"height\":%d,\"bytes\":%zu}\n", frame->width, frame->height, frame->bgra.size());
            std::fflush(stdout); app.exit(0);
        }
    });
    timer.start(20);
    QTimer::singleShot(60000, &app, [&] { app.exit(4); });
    return app.exec();
}
