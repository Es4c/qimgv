#pragma once

#include <QThreadPool>
#include <QHash>
#include <memory>
#include "loaderrunnable.h"

class Loader : public QObject {
    Q_OBJECT

    friend class LoaderRunnable;

public:
    explicit Loader();
    ~Loader();

    std::shared_ptr<Image> load(const QString &path);
    void loadAsyncPriority(const QString &path);
    void loadAsync(const QString &path);
    void clearTasks();
    
    bool isBusy() const;
    bool isLoading(const QString &path);

signals:
    void loadFinished(std::shared_ptr<Image>, const QString &path);
    void loadFailed(const QString &path);

private:
    QHash<QString, std::shared_ptr<LoaderRunnable>> tasks;
    QThreadPool *pool;         // 预加载线程池
    QThreadPool *priorityPool; // 当前图片专用，避免被运行中的 preload 卡住

    void doLoadAsync(QThreadPool *targetPool, const QString &path);
    void onLoadFinished(const std::shared_ptr<Image> &image, const QString &path);
};
