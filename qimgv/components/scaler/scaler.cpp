#include "scaler.h"
#include <QMutexLocker>

Scaler::Scaler(Cache *_cache, QObject *parent)
    : QObject(parent),
      buffered(false),
      running(false),
      cache(_cache)
{
    pool = new QThreadPool(this);
    pool->setMaxThreadCount(1);
}

Scaler::~Scaler() {
    pool->waitForDone();
}

void Scaler::requestScaled(const ScalerRequest &req) {
    bool needImmediateStart = false;
    ScalerRequest stamped = req;

    {
        QMutexLocker locker(&mutex);

        // 🚀 每次请求递增代数并打戳，排队中任务据此自检是否已过期
        stamped.setGeneration(mGeneration.fetch_add(1, std::memory_order_relaxed) + 1);
        bufferedRequest = stamped;

        if (!running) {
            if (!buffered) {
                buffered = true;
                needImmediateStart = true;
            }
        } else {
            if (!buffered) {
                buffered = true;
            }
        }
    }

    if (needImmediateStart) {
        startRequest(stamped);
    }
}

void Scaler::startRequest(const ScalerRequest& req) {
    auto *runnable = new ScalerRunnable(req, &mGeneration);
    runnable->setAutoDelete(true);

    connect(runnable, &ScalerRunnable::started,
            this, &Scaler::onTaskStart,
            Qt::DirectConnection);

    // ✅ 关键：worker 线程直接产出 QPixmap，UI 线程零深拷贝
    connect(runnable, &ScalerRunnable::finished,
            this, &Scaler::onTaskFinish,
            Qt::DirectConnection);

    pool->start(runnable);
}

void Scaler::onTaskStart(const ScalerRequest &req) {
    QMutexLocker locker(&mutex);

    running = true;

    if (buffered && bufferedRequest == req) {
        buffered = false;
    }

    startedRequest = req;
}

void Scaler::onTaskFinish(QPixmap scaled, ScalerRequest req) {
    bool deliverResult = false;
    bool hasNext = false;
    ScalerRequest nextReq;

    {
        QMutexLocker locker(&mutex);

        running = false;

        if (buffered) {
            if (!scaled.isNull() && bufferedRequest == startedRequest) {
                // 🚀 缓冲请求与刚完成的完全相同 → 结果直接复用，跳过重复缩放
                buffered = false;
                startedRequest = ScalerRequest();
                deliverResult = true;
            } else {
                // 取走缓冲中的最新请求。若启动前又被更新的请求覆盖，
                // 旧的排队任务会在 run() 开头按代数自检取消，不会白算
                nextReq = bufferedRequest;
                buffered = false;
                hasNext = true;
            }
        } else {
            deliverResult = !scaled.isNull();
            startedRequest = ScalerRequest();
        }
    }

    if (hasNext) {
        startRequest(nextReq);
    }

    if (deliverResult) {
        emit scalingFinished(std::move(scaled), std::move(req));
    }
}
