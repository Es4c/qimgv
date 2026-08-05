#include "loader.h"
#include <QThread>
#include <QMutableHashIterator>

Loader::Loader() {
    pool = new QThreadPool(this);
    pool->setMaxThreadCount(2);

    // 🚀 减少 QHash rehash
    tasks.reserve(32);
}

Loader::~Loader() {
    // 1. 取消排队中的任务（tryTake 成功则 delete）
    clearTasks();
    // 2. 等待运行中任务完成。此时 Loader 成员仍有效，
    //    onLoadFinished 若被调用也能安全访问 tasks（主线程事件循环已退出，通常不会执行）
    pool->waitForDone();
    // 3. 手动释放剩余任务（onLoadFinished 因 queued 未处理而残留的）
    //    先 disconnect 切断 queued 信号，防止事件循环意外恢复时回调已删除对象
    for (auto *runnable : tasks) {
        runnable->disconnect();
        delete runnable;
    }
    tasks.clear();
    // pool 作为子对象随后析构，QThreadPool 析构会再次 waitForDone（已结束，立即返回）
}

void Loader::clearTasks() {
    // 只取消"排队中"的任务，保留"运行中"的任务让其完成并进缓存
    // 这样 preload 在正常速度翻页时能真正生效（结果进缓存），
    // 同时快速翻页时排队中的过期 preload 仍会被取消，不浪费线程。
    QMutableHashIterator<QString, LoaderRunnable*> it(tasks);
    
    while (it.hasNext()) {
        it.next();
        LoaderRunnable *runnable = it.value();

        // tryTake 仅对尚未启动的任务返回 true
        if (pool->tryTake(runnable)) {
            // 任务还在队列中，直接删除对象并从哈希表移除
            delete runnable;
            it.remove();
        }
        // 运行中的任务保留在 tasks 中，完成后 onLoadFinished 会收到信号并进缓存
    }
}

bool Loader::isBusy() const {
    return !tasks.isEmpty();
}

bool Loader::isLoading(const QString &path) {
    return tasks.contains(path);
}

std::shared_ptr<Image> Loader::load(const QString &path) {
    return ImageFactory::createImage(path);
}

void Loader::loadAsyncPriority(const QString &path) {
    clearTasks(); // 清理当前任务，优先加载新任务
    doLoadAsync(path, 1);
}

void Loader::loadAsync(const QString &path) {
    doLoadAsync(path, 0);
}

void Loader::doLoadAsync(const QString &path, int priority) {
    auto it = tasks.find(path);
    if (it != tasks.end()) {
        return; // 已在加载中
    }
    
    auto *runnable = new LoaderRunnable(path);
    runnable->setAutoDelete(false); // 我们手动管理内存，以便在 clearTasks 时安全删除
    
    tasks.insert(path, runnable);
    connect(runnable, &LoaderRunnable::finished, this, &Loader::onLoadFinished);
    
    pool->start(runnable, priority);
}

void Loader::onLoadFinished(const std::shared_ptr<Image> &image, const QString &path) {
    auto *task = tasks.take(path);
    if (task) {
        // 转发结果（运行中的 preload 任务完成后结果会进缓存）
        if (!image) emit loadFailed(path);
        else emit loadFinished(image, path);
        task->deleteLater(); // 安全删除，避免跨线程 delete 竞态
    }
    // 任务不在 tasks 中：已被 clearTasks 通过 tryTake 取消并删除，无需处理
}
