#include "dummywatcher.h"
#include "directorywatcher_p.h"
#include <QDebug>

#define TAG         "[DummyWatcher]"
#define MESSAGE     "Directory watcher isn't yet implemented for your operating system"

class DummyWatcherWorker : public WatcherWorker {
  public:
    DummyWatcherWorker() = default;
    virtual void run() override {
        qDebug() << TAG << MESSAGE;
    }
};

class DummyWatcherPrivate : public DirectoryWatcherPrivate {
  public:
    DummyWatcherPrivate(DirectoryWatcher* watcher) : DirectoryWatcherPrivate(watcher, new DummyWatcherWorker()) {}
};

DummyWatcher::DummyWatcher() : DirectoryWatcher(new DummyWatcherPrivate(this))
{
    qDebug() << TAG << MESSAGE;
}

bool DummyWatcher::isWatchPathReady(const QString& path) const
{
    // 无真实监视可等待：扫描直接开始，避免非 Windows 平台白等超时
    Q_UNUSED(path);
    return true;
}

void DummyWatcher::observe()
{
    DirectoryWatcher::observe();
    emit watchPathReady(watchPath());
}

void DummyWatcher::requestWatchPath(const QString& path)
{
    setWatchPath(path);
    emit watchPathReady(path);
}
