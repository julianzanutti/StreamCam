#pragma once

#include "AudioPublisher.h"
#include "../media/LatestAudio.h"
#include <QCoreApplication>
#include <QMetaObject>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <atomic>
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
            publisher_=new AudioPublisher(worker_);
            auto meterFreshness=std::make_shared<QElapsedTimer>();
            if(!publisher_->start(socketName,socketOptions) && onServerError) {
                if(auto *application=QCoreApplication::instance()) {
                    const auto error=publisher_->errorString();
                    QMetaObject::invokeMethod(application,[onServerError,error]{onServerError(error);},Qt::QueuedConnection);
                }
            }
            auto *timer=new QTimer(worker_);
            timer->setInterval(10);
            QObject::connect(timer,&QTimer::timeout,worker_,[this,meterFreshness] {
                const auto source=source_.load();
                bool delivered=false;
                if(source)for(const auto &audio:source->drain(5)) delivered=publisher_->publish(audio)||delivered;
                if(delivered)meterFreshness->start();
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
    void clear() {
        if(const auto source=source_.load())source->clear();
        clearPublisher();
    }
    std::uint64_t droppedFrames() const {return dropped_.load();}
    float outputLevelDbfs() const {return levelDbfs_.load();}
    void stop() {
        source_.store({});
        if(!thread_.isRunning())return;
        thread_.quit();thread_.wait();
        worker_=nullptr;publisher_=nullptr;dropped_.store(0);levelDbfs_.store(-120.0F);
    }

private:
    void clearPublisher() {
        if(!worker_ || !thread_.isRunning())return;
        QMetaObject::invokeMethod(worker_,[this] {if(publisher_)publisher_->clear();},Qt::QueuedConnection);
    }
    std::atomic<std::shared_ptr<LatestAudio>> source_;
    std::atomic<std::uint64_t> dropped_{0};
    std::atomic<float> levelDbfs_{-120.0F};
    QThread thread_;
    QObject *worker_=nullptr;
    AudioPublisher *publisher_=nullptr; // Accessed only on the worker thread.
};
}
