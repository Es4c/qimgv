#pragma once

#include <QObject>
#include <QThreadPool>
#include <QMutex>
#include <QPixmap>
#include <atomic>
#include "components/cache/cache.h"
#include "scalerrequest.h"
#include "scalerrunnable.h"

class Scaler : public QObject {
    Q_OBJECT
public:
    explicit Scaler(Cache *_cache, QObject *parent = nullptr);
    ~Scaler() override;

signals:
    // ✅ QPixmap 由 worker 线程产出（隐式共享），经 queued 连接浅拷贝送达 UI 线程
    void scalingFinished(QPixmap result, ScalerRequest request);

public slots:
    void requestScaled(const ScalerRequest &req);

private slots:
    void onTaskStart(const ScalerRequest &req);

    void onTaskFinish(QPixmap scaled, ScalerRequest req);

private:
    void startRequest(const ScalerRequest &req);

    QThreadPool *pool;
    Cache *cache;

    bool buffered;
    bool running;

    ScalerRequest bufferedRequest;
    ScalerRequest startedRequest;

    // 🚀 请求代数计数：requestScaled 时递增打戳，排队中任务据此自检是否已过期
    std::atomic<quint64> mGeneration{0};

    mutable QMutex mutex;
};