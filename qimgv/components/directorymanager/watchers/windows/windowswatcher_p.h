#ifndef WINDOWSWATCHER_P_H
#define WINDOWSWATCHER_P_H

#include "windowswatcher.h"
#include "../directorywatcher_p.h"
#include "windowsworker.h"
#include <QQueue>
#include <windows.h>

// 错误信息构造（错误路径，非热点）
static inline QString lastError()
{
    DWORD lastError = GetLastError(); // 保存原始错误码
    wchar_t buffer[512];
    DWORD res = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, lastError,
                               LANG_SYSTEM_DEFAULT, buffer, 512, nullptr);
    if (res == 0) {
        return QString::number(lastError); // 使用保存的错误码
    }
    return QStringLiteral("%1::%2: %3").arg(
        QString::fromLatin1(__FILE__), QString::number(__LINE__),
        QString::fromWCharArray(buffer, static_cast<int>(res)));
}

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
