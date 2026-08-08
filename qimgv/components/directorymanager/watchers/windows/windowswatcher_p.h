#ifndef WINDOWSWATCHER_P_H
#define WINDOWSWATCHER_P_H

#include "windowswatcher.h"
#include "../directorywatcher_p.h"
#include "windowsworker.h"
#include <QQueue>
#include <QString>
#include <windows.h>

class WindowsWatcherPrivate : public DirectoryWatcherPrivate
{
    Q_OBJECT
    Q_DECLARE_PUBLIC(WindowsWatcher)

public:
    explicit WindowsWatcherPrivate(WindowsWatcher* qq);

public slots:
    // ⭐ 批量派发
    void dispatchNotify(const QVector<NotifyEvent>& events);

private:
    // ⭐ 使用队列保证 rename 配对正确（关键修复）
    QQueue<QString> renameOldQueue;
};

#endif // WINDOWSWATCHER_P_H
