// watcherworker.h
#pragma once

#include <QObject>
#include <atomic>

class WatcherWorker : public QObject
{
    Q_OBJECT
public:
    WatcherWorker() = default;
    virtual ~WatcherWorker() = default;

public Q_SLOTS:
    virtual void run() = 0;               // 改为槽，允许跨线程调用
    // 虚函数：具体 worker 需要在停止时唤醒 I/O / 清除就绪状态
    virtual void setRunning(bool running) {
        isRunning.store(running, std::memory_order_release);
    }

Q_SIGNALS:
    void error(const QString &errorMessage);
    void started();
    void finished();

protected:
    std::atomic<bool> isRunning{false};
};