// windowswatcher.cpp
#include "windowswatcher_p.h"
#include "windowsworker.h"

WindowsWatcherPrivate::WindowsWatcherPrivate(WindowsWatcher* qq)
    : DirectoryWatcherPrivate(static_cast<DirectoryWatcher*>(qq), new WindowsWorker())
{
    auto windowsWorker = static_cast<WindowsWorker*>(worker.data());
    // 排队连接需要 QVector<NotifyEvent> 的元类型
    qRegisterMetaType<QVector<NotifyEvent>>("QVector<NotifyEvent>");
    connect(windowsWorker, &WindowsWorker::notifyEvents, this, &WindowsWatcherPrivate::dispatchNotify);
}

void WindowsWatcherPrivate::dispatchNotify(const QVector<NotifyEvent>& events)
{
    Q_Q(WindowsWatcher);

    for (const NotifyEvent& ev : events) {
        switch (ev.action) {
        case FILE_ACTION_ADDED:
            emit q->fileCreated(ev.fileName);
            break;

        case FILE_ACTION_MODIFIED:
            emit q->fileModified(ev.fileName);
            break;

        case FILE_ACTION_REMOVED:
            emit q->fileDeleted(ev.fileName);
            break;

        case FILE_ACTION_RENAMED_OLD_NAME:
            renameOldQueue.enqueue(ev.fileName);
            break;

        case FILE_ACTION_RENAMED_NEW_NAME:
            if (!renameOldQueue.isEmpty()) {
                const QString oldName = renameOldQueue.dequeue();
                emit q->fileRenamed(oldName, ev.fileName);
            } else {
                // fallback：极端情况下丢失 OLD，只当作创建处理
                emit q->fileCreated(ev.fileName);
            }
            break;

        default:
            break;
        }
    }
}

WindowsWatcher::WindowsWatcher(QObject* parent)
    : DirectoryWatcher(new WindowsWatcherPrivate(this))
{
    Q_D(WindowsWatcher);
    auto windowsWorker = static_cast<WindowsWorker*>(d->worker.data());
    connect(windowsWorker, &WindowsWorker::finished, d->workerThread.data(), &QThread::quit);
    connect(windowsWorker, &WindowsWorker::started, this, &WindowsWatcher::observingStarted);
    connect(windowsWorker, &WindowsWorker::finished, this, &WindowsWatcher::observingStopped);
}

void WindowsWatcher::setWatchPath(const QString &path)
{
    Q_D(WindowsWatcher);
    DirectoryWatcher::setWatchPath(path);

    auto windowsWorker = static_cast<WindowsWorker*>(d->worker.data());
    if (windowsWorker) {
        windowsWorker->setWatchPath(path);
    }
}

void WindowsWatcher::stopObserving()
{
    Q_D(WindowsWatcher);
    if (!d->workerThread || !d->workerThread->isRunning())
        return;

    // 先中断阻塞的 I/O，让工作线程能快速退出
    cancelIo();
    DirectoryWatcher::stopObserving();
}

void WindowsWatcher::requestWatchPath(const QString& path)
{
    Q_D(WindowsWatcher);
    DirectoryWatcher::setWatchPath(path);

    auto windowsWorker = static_cast<WindowsWorker*>(d->worker.data());
    if (windowsWorker) {
        // 直接调用而非 invokeMethod，避免依赖事件队列
        windowsWorker->requestDirectoryHandle(path);
    }
}

void WindowsWatcher::cancelIo()
{
    Q_D(WindowsWatcher);
    auto windowsWorker = static_cast<WindowsWorker*>(d->worker.data());
    if (windowsWorker) {
        // 直接调用而非 invokeMethod
        windowsWorker->cancelIo();
    }
}
