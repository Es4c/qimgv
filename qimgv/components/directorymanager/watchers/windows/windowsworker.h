// windowsworker.h
#ifndef WINDOWSWORKER_H
#define WINDOWSWORKER_H

#include "../watcherworker.h"
#include <windows.h>
#include <QString>
#include <QByteArray>
#include <QVector>
#include <QMetaType>
#include <QMutex>
#include <atomic>
#include <algorithm>
#include <utility>

class ScopedHandle {
public:
    ScopedHandle() noexcept = default;
    explicit ScopedHandle(HANDLE h) noexcept : handle_(h) {}
    ~ScopedHandle() noexcept { close(); }

    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;

    ScopedHandle(ScopedHandle&& other) noexcept : handle_(other.release()) {}
    ScopedHandle& operator=(ScopedHandle&& other) noexcept {
        if (this != &other) {
            close();
            handle_ = other.release();
        }
        return *this;
    }

    void reset(HANDLE h = INVALID_HANDLE_VALUE) noexcept { close(); handle_ = h; }

    void close() noexcept {
        // CreateEventW 失败返回 NULL，CreateFileW 失败返回 INVALID_HANDLE_VALUE，
        // 两者都不能当有效句柄关闭。
        if (handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr) {
            CloseHandle(handle_);
        }
        handle_ = INVALID_HANDLE_VALUE;
    }

    HANDLE get() const noexcept { return handle_; }
    HANDLE release() noexcept { return std::exchange(handle_, INVALID_HANDLE_VALUE); }
    explicit operator bool() const noexcept {
        return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr;
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

// 一次 ReadDirectoryChangesW 返回的单个通知条目
struct NotifyEvent {
    QString directoryPath;   // 产生该通知的监视目录（切换路径后仍能正确归属）
    QString fileName;
    DWORD action;
};
Q_DECLARE_METATYPE(NotifyEvent)

class WindowsWorker : public WatcherWorker {
    Q_OBJECT
public:
    explicit WindowsWorker();
    ~WindowsWorker() override = default;
    void run() override;
    void setRunning(bool running) override;
    void setWatchPath(const QString& path);
    // 这些方法可被任意线程直接调用，句柄通过原子变量暴露，路径经互斥锁保护
    void requestDirectoryHandle(const QString& path);
    bool isWatchPathReady(const QString& path) const;

signals:
    // ⭐ 批量派发，避免每个事件一个排队信号
    void notifyEvents(const QVector<NotifyEvent>& events);
    // ⭐ 只有 ReadDirectoryChangesW 已成功发起后才发出：该次 arm 之后的变化由它接收
    void watchPathReady(const QString& directoryPath);
    void watchPathFailed(const QString& directoryPath);
    void finished();
    void started();

private:
    HANDLE openDirectoryHandle(const QString& path);
    // ⭐ run() 拆分：每 helper 单一职责，run 仅保留主循环编排
    bool ensureInitialHandle(QString &currentPath);
    // 返回 false 表示应退出主循环
    bool handleRestart(QString &currentPath, int &quietReads);
    // 发起一次异步 ReadDirectoryChangesW；成功返回时监视已经生效
    bool armRead(const QString& directoryPath);
    void cancelPendingRead();
    void setReadyPath(const QString& path);
    enum class WaitResult { Ok, Stop, Retry };
    WaitResult waitOnce(DWORD &bytesReturned);
    QVector<NotifyEvent> parseBatch(DWORD bytesReturned, const QString &directoryPath);
    static void dedupModified(QVector<NotifyEvent> &batch);
    void emitAndMaybeShrink(const QVector<NotifyEvent> &batch, DWORD bytesReturned, int &quietReads);

    ScopedHandle hDirectory;
    std::atomic<HANDLE> activeHandle{INVALID_HANDLE_VALUE};
    QString pendingPath;
    QByteArray buffer;
    bool networkBufferCapped = false;   // ⭐ 网络目录(SMB)缓冲上限 64KB，仅 worker 线程访问
    std::atomic<bool> needsRestart{false};
    // 异步读取：hEvent 用于等待完成，wakeEvent 用于 stop/restart 立即唤醒
    ScopedHandle readEvent;
    ScopedHandle wakeEvent;
    OVERLAPPED overlapped{};
    bool readPending = false;           // 仅 worker 线程访问
    bool announceReady = true;          // 下一次成功 arm 后发 watchPathReady（仅 worker）
    mutable QMutex pathMutex;           // 保护 pendingPath 与 readyPath
    QString readyPath;                  // 已成功挂上监视的目录，受 pathMutex 保护
};

#endif // WINDOWSWORKER_H
