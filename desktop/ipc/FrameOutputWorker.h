#pragma once

#include "FramePublisher.h"
#include "../media/LatestFrame.h"
#include <QCoreApplication>
#include <QMetaObject>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <functional>
#include <limits>
#include <memory>

namespace streamcam {
// Drains the newest decoded frame independently of preview/UI timers.
class FrameOutputWorker final {
public:
    FrameOutputWorker() = default;
    FrameOutputWorker(const FrameOutputWorker&) = delete;
    FrameOutputWorker& operator=(const FrameOutputWorker&) = delete;
    ~FrameOutputWorker() { stop(); }

    void start(std::function<void(const QString&)> onServerError = {},const QString &socketName=QStringLiteral("StreamCamFrames-v1"),
               QLocalServer::SocketOptions socketOptions=QLocalServer::UserAccessOption) {
        if (thread_.isRunning()) return;
        worker_ = new QObject;
        worker_->moveToThread(&thread_);
        QObject::connect(&thread_, &QThread::started, worker_, [this, onServerError=std::move(onServerError),socketName,socketOptions] {
            publisher_ = new FramePublisher(worker_);
            if (!publisher_->start(socketName,socketOptions) && onServerError) {
                if (auto *application=QCoreApplication::instance()) {
                    const auto error=publisher_->errorString();
                    QMetaObject::invokeMethod(application,[onServerError,error] {onServerError(error);},Qt::QueuedConnection);
                }
            }
            auto *timer = new QTimer(worker_);
            timer->setInterval(5);
            auto lastTimestamp=std::make_shared<std::int64_t>(std::numeric_limits<std::int64_t>::min());
            QObject::connect(timer, &QTimer::timeout, worker_, [this, lastTimestamp] {
                const auto source=source_.load();
                const auto frame=source ? source->read() : nullptr;
                if (!frame || frame->timestampUs==*lastTimestamp) return;
                *lastTimestamp=frame->timestampUs;
                publisher_->publish(frame);
            });
            timer->start();
        });
        QObject::connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
        thread_.setObjectName(QStringLiteral("StreamCam OBS video output"));
        thread_.start();
    }

    void setSource(std::shared_ptr<LatestFrame> source) {
        source_.store(std::move(source));
        clearPublisher();
    }

    void clear() {
        if (const auto source=source_.load()) source->clear();
        clearPublisher();
    }

    void stop() {
        source_.store({});
        if (!thread_.isRunning()) return;
        thread_.quit();
        thread_.wait();
        worker_=nullptr;
        publisher_=nullptr;
    }

private:
    void clearPublisher() {
        if (!worker_ || !thread_.isRunning()) return;
        QMetaObject::invokeMethod(worker_,[this] { if (publisher_) publisher_->clear(); },Qt::QueuedConnection);
    }

    std::atomic<std::shared_ptr<LatestFrame>> source_;
    QThread thread_;
    QObject *worker_=nullptr;
    FramePublisher *publisher_=nullptr; // Accessed only on the worker thread.
};
}
