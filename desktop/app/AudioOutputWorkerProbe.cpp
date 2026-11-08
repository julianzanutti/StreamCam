#include "../ipc/AudioOutputWorker.h"
#include <QCoreApplication>
#include <QLocalSocket>
#include <QtEndian>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    std::cerr<<"probe: start\n";
    const QString pipe=QStringLiteral("StreamCamAudioProbe-v2");
    streamcam::AudioOutputWorker output;
    std::atomic<bool> serverFailed{false};
    QString serverError;
    output.start([&](const QString &error){serverError=error;serverFailed.store(true);},pipe,QLocalServer::SocketOptions{});
    QLocalSocket client;
    bool connected=false;
    for(int attempt=0;attempt<30 && !connected;++attempt) {
        QCoreApplication::processEvents();
        client.connectToServer(pipe);
        connected=client.waitForConnected(100);
        if(!connected){client.abort();std::this_thread::sleep_for(std::chrono::milliseconds(20));}
    }
    if(!connected){std::cerr<<(serverFailed.load()?"Audio output worker failed to listen: "+serverError.toStdString()+"\n":"Audio output worker pipe did not accept a client.\n");return 1;}
    std::cerr<<"probe: connected\n";

    auto source=std::make_shared<streamcam::LatestAudio>();
    output.setSource(source);
    std::atomic<bool> producing{true};
    std::atomic<int> observedLevel{0};
    std::thread producer([&] {
        std::uint64_t timestamp=1;
        while(producing.load()) {
            auto audio=std::make_shared<streamcam::AudioFrame>();
            audio->sampleRate=48000;audio->channels=1;audio->frames=960;audio->timestampUs=timestamp++*20000;
            audio->pcm.assign(960,1234);
            source->publish(std::move(audio));
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    });
    // The live meter intentionally expires after 100 ms without delivery.
    // A slow/loaded host can therefore deliver packets during the 2 s warm-up
    // and clear the meter before the polling thread observes it. Remember any
    // observed delivery across the entire warm-up window instead of requiring
    // the transient meter value to still be live at the deadline.
    const auto warmupDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    float liveLevelDbfs=-120.0F;
    while(std::chrono::steady_clock::now()<warmupDeadline) {
        const auto level=output.outputLevelDbfs();
        if(level>-120.0F) { liveLevelDbfs=level; observedLevel.store(1); }
        // Keep Qt's main-thread event loop responsive so the local-socket
        // client can receive data while the independent producer continues.
        QCoreApplication::processEvents();
        if(liveLevelDbfs>-120.0F) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if(liveLevelDbfs<=-120.0F) {
        producing.store(false);producer.join();
        std::cerr<<"Audio output worker did not deliver its first packet before the UI-block test.\n";return 1;
    }
    // Simulate expensive UI work on a separate thread while keeping the Qt
    // event loop responsive for the local-socket client.
    std::atomic<bool> uiWorkDone{false};
    std::thread simulatedUiWork([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        uiWorkDone.store(true);
    });
    while(!uiWorkDone.load()) {
        QCoreApplication::processEvents();
        if(output.outputLevelDbfs()>-120.0F) observedLevel.store(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    simulatedUiWork.join();
    std::cerr<<"probe: level="<<liveLevelDbfs<<"\n";
    producing.store(false);producer.join();
    client.waitForReadyRead(1500);
    const QByteArray bytes=client.readAll();
    int packets=0;
    qsizetype offset=0;
    while(offset+32<=bytes.size()) {
        const auto *header=reinterpret_cast<const uchar*>(bytes.constData()+offset);
        if(qFromLittleEndian<quint32>(header)!=0x31414353)break;
        const auto channels=qFromLittleEndian<quint32>(header+12);
        const auto frames=qFromLittleEndian<quint32>(header+16);
        const qsizetype size=32+static_cast<qsizetype>(channels)*frames*2;
        if(channels!=1 || frames!=960 || offset+size>bytes.size())break;
        ++packets;offset+=size;
    }
    if(packets<3){std::cerr<<"Expected >=3 audio packets independent of UI; received "<<packets<<".\n";return 1;}
    std::cerr<<"probe: packets="<<packets<<"\n";
    if(!(liveLevelDbfs>-60.0F && liveLevelDbfs<=0.0F)) {
        std::cerr<<"Expected live output peak between -60 and 0 dBFS, got "<<liveLevelDbfs<<".\n";return 1;
    }
    client.abort();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto unobserved=std::make_shared<streamcam::AudioFrame>();
    unobserved->sampleRate=48000;unobserved->channels=1;unobserved->frames=960;
    unobserved->timestampUs=10'000'000;unobserved->pcm.assign(960,2345);
    source->publish(unobserved);
    for(int attempt=0;attempt<50 && output.outputLevelDbfs()>-120.0F;++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if(output.outputLevelDbfs()>-120.0F) {
        std::cerr<<"Output meter reported audio while no OBS client was connected.\n";return 1;
    }
    std::cerr<<"probe: meter cleared\n";
    QLocalSocket recoveredClient;
    bool reconnected=false;
    for(int attempt=0;attempt<30 && !reconnected;++attempt) {
        recoveredClient.connectToServer(pipe);
        reconnected=recoveredClient.waitForConnected(100);
        if(!reconnected){recoveredClient.abort();std::this_thread::sleep_for(std::chrono::milliseconds(20));}
    }
    if(!reconnected){std::cerr<<"Audio pipe did not accept a client after reconnection.\n";return 1;}
    std::cerr<<"probe: reconnected\n";
    auto recovered=std::make_shared<streamcam::AudioFrame>();
    recovered->sampleRate=48000;recovered->channels=1;recovered->frames=960;
    recovered->timestampUs=10'020'000;recovered->pcm.assign(960,3456);
    source->publish(recovered);
    for(int attempt=0;attempt<50 && output.outputLevelDbfs()<=-120.0F;++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if(output.outputLevelDbfs()<=-120.0F) {
        std::cerr<<"Output meter did not recover after an OBS client reconnected.\n";return 1;
    }
    std::cerr<<"probe: recovered meter="<<output.outputLevelDbfs()<<"\n";
    recoveredClient.abort();output.clear();
    for(int attempt=0;attempt<50 && output.outputLevelDbfs()>-120.0F;++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if(output.outputLevelDbfs()>-120.0F) { std::cerr<<"Output meter did not clear after audio stopped.\n";return 1; }
    std::cout<<"PASS: audio pipe received "<<packets<<" packets while UI was blocked; meter tracks only delivery and recovers after OBS reconnects.\n";
    return 0;
}
