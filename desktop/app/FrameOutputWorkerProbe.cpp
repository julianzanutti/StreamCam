#include "../ipc/FrameOutputWorker.h"
#include <QCoreApplication>
#include <QLocalSocket>
#include <QtEndian>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    const QString pipe=QStringLiteral("StreamCamFramesProbe-v2");
    streamcam::FrameOutputWorker output;
    std::atomic<bool> serverFailed{false};
    QString serverError;
    output.start([&](const QString &error) {serverError=error;serverFailed.store(true);},pipe,QLocalServer::SocketOptions{});
    QLocalSocket client;
    bool connected=false;
    for(int attempt=0;attempt<30 && !connected;++attempt) {
        QCoreApplication::processEvents();
        client.connectToServer(pipe);
        connected=client.waitForConnected(100);
        if(!connected) {client.abort();std::this_thread::sleep_for(std::chrono::milliseconds(20));}
    }
    if(!connected) {std::cerr<<(serverFailed.load()?"Frame output worker failed to listen: "+serverError.toStdString()+"\n":"Frame output worker pipe did not accept a client.\n");return 1;}

    auto source=std::make_shared<streamcam::LatestFrame>();
    output.setSource(source);
    std::atomic<bool> producing{true};
    std::thread producer([&] {
        std::int64_t timestamp=1;
        while(producing.load()) {
            auto frame=std::make_shared<streamcam::VideoFrame>();
            frame->width=4;frame->height=4;frame->stride=16;frame->timestampUs=timestamp++;
            frame->bgra.assign(64,0x7f);
            source->publish(std::move(frame));
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
    });
    // Simulate a blocked preview/UI thread while OBS output keeps draining.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    producing.store(false);producer.join();
    client.waitForReadyRead(1500);
    QByteArray bytes=client.readAll();
    int frames=0;
    qsizetype offset=0;
    while(offset+40<=bytes.size()) {
        const auto *header=reinterpret_cast<const uchar*>(bytes.constData()+offset);
        if(qFromLittleEndian<quint32>(header)!=0x314d4353)break;
        const auto payload=qFromLittleEndian<quint32>(header+20);
        if(offset+40+payload>bytes.size())break;
        ++frames;offset+=40+payload;
    }
    output.clear();
    client.disconnectFromServer();
    if(frames<3) {std::cerr<<"Expected >=3 independent output frames; received "<<frames<<".\n";return 1;}
    std::cout<<"PASS: OBS pipe received "<<frames<<" frames while the producer/UI thread was blocked.\n";
    return 0;
}
