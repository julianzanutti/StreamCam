#pragma once

#include "AudioPublisher.h"
#include "../media/WindowsAudioOutput.h"
#include "../media/LatestAudio.h"
#include <QCoreApplication>
#include <QMetaObject>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <avrt.h>
#include <functional>
#include <memory>

namespace streamcam {
// Processes and publishes the bounded audio queue away from the UI thread.
class AudioOutputWorker final {
public:
    AudioOutputWorker() = default;
    AudioOutputWorker(const AudioOutputWorker&) = delete;
    AudioOutputWorker& operator=(const AudioOutputWorker&) = delete;
    ~AudioOutputWorker() { stop(); }

    void start(std::function<void(const QString&)> onServerError = {},
               const QString &socketName=QStringLiteral("StreamCamAudio-v1"),
               QLocalServer::SocketOptions socketOptions=QLocalServer::UserAccessOption) {
        if(thread_.isRunning())return;
        worker_=new QObject;
        worker_->moveToThread(&thread_);
        QObject::connect(&thread_,&QThread::started,worker_,[this,onServerError=std::move(onServerError),socketName,socketOptions] {
            DWORD taskIndex=0;
            const auto multimediaTask=AvSetMmThreadCharacteristicsW(L"Audio",&taskIndex);
            if(multimediaTask)QObject::connect(worker_,&QObject::destroyed,[multimediaTask] {AvRevertMmThreadCharacteristics(multimediaTask);});
            publisher_=new AudioPublisher(worker_);
            deviceOutput_=new WindowsAudioOutput;
            publisher_->directOutput=[this](const std::int16_t *pcm,unsigned frames,unsigned channels) { if(!deviceOutput_->write(pcm,frames,channels) && directStatus_.load()==0 && deviceOutput_->error()!=0) {
                directStatus_.store(static_cast<int>(deviceOutput_->error()));deviceOutput_->close();
            } directDropped_.store(deviceOutput_->dropped()); };
            QObject::connect(worker_,&QObject::destroyed,[this] {delete deviceOutput_;deviceOutput_=nullptr;});
            auto meterFreshness=std::make_shared<QElapsedTimer>();
            if(!publisher_->start(socketName,socketOptions) && onServerError) {
                if(auto *application=QCoreApplication::instance()) {
                    const auto error=publisher_->errorString();
                    QMetaObject::invokeMethod(application,[onServerError,error]{onServerError(error);},Qt::QueuedConnection);
                }
            }
            auto *timer=new QTimer(worker_);
            timer->setTimerType(Qt::PreciseTimer);
            timer->setInterval(10);
            QObject::connect(timer,&QTimer::timeout,worker_,[this,meterFreshness] {
                const auto source=source_.load();
                bool delivered=false;
                if(source)for(const auto &audio:source->drain(5)) delivered=publisher_->publish(audio)||delivered;
                if(delivered)meterFreshness->start();
                obsClients_.store(static_cast<unsigned>(publisher_->clientCount()));
                dropped_.store(publisher_->droppedFrames());
                levelDbfs_.store(meterFreshness->isValid() && meterFreshness->elapsed()<100
                    ? publisher_->outputPeakDbfs() : -120.0F);
            });
            timer->start();
        });
        QObject::connect(&thread_,&QThread::finished,worker_,&QObject::deleteLater);
        thread_.setObjectName(QStringLiteral("StreamCam OBS audio output"));
        thread_.start();
    }

    void setSource(std::shared_ptr<LatestAudio> source) {
        source_.store(std::move(source));
        clearPublisher();
    }
    void setVoiceSettings(const VoiceSettings &settings) {
        if(!worker_ || !thread_.isRunning())return;
        QMetaObject::invokeMethod(worker_,[this,settings] {if(publisher_)publisher_->setVoiceSettings(settings);},Qt::QueuedConnection);
    }
    void setOutputDevice(int id,const QString &endpointId={}) {
        if(!worker_ || !thread_.isRunning())return;
        QMetaObject::invokeMethod(worker_,[this,id,endpointId] {
            if(!deviceOutput_)return;
            if(id<0) {deviceOutput_->close();directStatus_.store(-1);}
            else {const bool opened=deviceOutput_->open(static_cast<unsigned>(id),endpointId.toStdWString());directStatus_.store(opened?0:static_cast<int>(deviceOutput_->error()));}
        },Qt::QueuedConnection);
    }
    void clear() {
        if(const auto source=source_.load())source->clear();
        clearPublisher();
    }
    unsigned obsClientCount() const {return obsClients_.load();}
    std::uint64_t directDroppedFrames() const {return directDropped_.load();}
    int directOutputStatus() const {return directStatus_.load();}
    std::uint64_t droppedFrames() const {return dropped_.load();}
    float outputLevelDbfs() const {return levelDbfs_.load();}
    void stop() {
        source_.store({});
        if(!thread_.isRunning())return;
        thread_.quit();thread_.wait();
        obsClients_.store(0);directStatus_.store(-1);worker_=nullptr;publisher_=nullptr;dropped_.store(0);levelDbfs_.store(-120.0F);
    }

private:
    void clearPublisher() {
        if(!worker_ || !thread_.isRunning())return;
        QMetaObject::invokeMethod(worker_,[this] {if(publisher_)publisher_->clear();if(deviceOutput_)deviceOutput_->clear();},Qt::QueuedConnection);
    }
    std::atomic<unsigned> obsClients_{0};
    std::atomic<std::uint64_t> directDropped_{0};
    std::atomic<int> directStatus_{-1}; // -1 disabled, 0 open, positive Windows audio error.
    std::atomic<std::shared_ptr<LatestAudio>> source_;
    std::atomic<std::uint64_t> dropped_{0};
    std::atomic<float> levelDbfs_{-120.0F};
    QThread thread_;
    QObject *worker_=nullptr;
    WindowsAudioOutput *deviceOutput_=nullptr; // Audio worker only.
    AudioPublisher *publisher_=nullptr; // Accessed only on the worker thread.
};
}
