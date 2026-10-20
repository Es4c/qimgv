// windowswatcher.cpp
#include "windowswatcher_p.h"
#include "windowsworker.h"

namespace {
constexpr int kMaxRenameQueueSize = 128;   // 重命名配对队列上限，防止 OLD 无对应 NEW 时无限累积
}

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
            emit q->fileCreated(ev.directoryPath, ev.fileName);
            break;

        case FILE_ACTION_MODIFIED:
            emit q->fileModified(ev.directoryPath, ev.fileName);
            break;

        case FILE_ACTION_REMOVED:
            emit q->fileDeleted(ev.directoryPath, ev.fileName);
            break;

        case FILE_ACTION_RENAMED_OLD_NAME:
            // 切换监视目录后，旧目录遗留的 OLD 永远等不到同目录的 NEW：
            // 直接丢弃，避免与新目录的 NEW 错配出"假重命名"
            while (!renameOldQueue.isEmpty()
                   && renameOldQueue.head().first != ev.directoryPath)
                renameOldQueue.dequeue();
            // ⭐ 队列上限：改名/移出目录只产生 OLD 而不会再有 NEW，长期累积会占内存。
            // 挤出的最旧项视为已删除（该旧名字已不在其监视目录中）
            if (renameOldQueue.size() >= kMaxRenameQueueSize) {
                const auto stale = renameOldQueue.dequeue();
                emit q->fileDeleted(stale.first, stale.second);
            }
            renameOldQueue.enqueue({ev.directoryPath, ev.fileName});
            break;

        case FILE_ACTION_RENAMED_NEW_NAME:
            while (!renameOldQueue.isEmpty()
                   && renameOldQueue.head().first != ev.directoryPath)
                renameOldQueue.dequeue();
            if (!renameOldQueue.isEmpty()) {
                const auto oldEntry = renameOldQueue.dequeue();
                emit q->fileRenamed(ev.directoryPath, oldEntry.second, ev.fileName);
            } else {
                // fallback：极端情况下丢失 OLD，只当作创建处理
                emit q->fileCreated(ev.directoryPath, ev.fileName);
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
    connect(windowsWorker, &WindowsWorker::watchPathReady,
            this, &DirectoryWatcher::watchPathReady);
    connect(windowsWorker, &WindowsWorker::watchPathFailed,
            this, &DirectoryWatcher::watchPathFailed);
    connect(windowsWorker, &WindowsWorker::started, this, &WindowsWatcher::observingStarted);
    connect(windowsWorker, &WindowsWorker::finished, this, &WindowsWatcher::observingStopped);
}

bool WindowsWatcher::isWatchPathReady(const QString &path) const
{
    Q_D(const WindowsWatcher);
    const auto *windowsWorker =
        static_cast<const WindowsWorker*>(d->worker.constData());
    return windowsWorker && windowsWorker->isWatchPathReady(path);
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
