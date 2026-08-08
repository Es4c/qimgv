// windowsworker.cpp
#include "windowsworker.h"
#include "windowswatcher_p.h"
#include <cstddef>
#include <QThread>

namespace {
constexpr qsizetype kInitialBufferSize = 131072;            // 128KB
constexpr qsizetype kMaxBufferSize = 16 * 1024 * 1024;      // 16MB，防止无限膨胀
constexpr qsizetype kMaxFileNameLength = 4096;              // 单条文件名上限（WCHAR）
}

WindowsWorker::WindowsWorker() {
    buffer.resize(kInitialBufferSize);
}

void WindowsWorker::setRunning(bool running) {
    isRunning.store(running, std::memory_order_release);

    if (!running) {
        needsRestart.store(false, std::memory_order_release); // ⭐ 防止退出时误重启
        cancelIo();  // 中断阻塞 I/O
    }
}

void WindowsWorker::setWatchPath(const QString& path) {
    QMutexLocker locker(&pathMutex);
    pendingPath = path;
}

void WindowsWorker::cancelIo() {
    HANDLE hDir = activeHandle.load(std::memory_order_acquire);
    if (hDir != INVALID_HANDLE_VALUE) {
        CancelIoEx(hDir, nullptr);
    }
}

void WindowsWorker::requestDirectoryHandle(const QString& path) {
    bool shouldCancel = false;
    {
        QMutexLocker locker(&pathMutex);
        // 路径没变且句柄有效 → 无需重启
        if (path == pendingPath &&
            activeHandle.load(std::memory_order_acquire) != INVALID_HANDLE_VALUE) {
            return;
        }
        pendingPath = path;
        shouldCancel = (activeHandle.load(std::memory_order_acquire) != INVALID_HANDLE_VALUE);
    }
    needsRestart.store(true, std::memory_order_release);
    // 立即中断当前阻塞的 ReadDirectoryChangesW，让循环快速响应新路径
    if (shouldCancel) {
        cancelIo();
    }
}

HANDLE WindowsWorker::openDirectoryHandle(const QString& path) {
    HANDLE hDir = INVALID_HANDLE_VALUE;
    int delay = 10;  // 指数退避起始

    while (isRunning.load(std::memory_order_acquire) && hDir == INVALID_HANDLE_VALUE) {
        hDir = CreateFileW(
            reinterpret_cast<LPCWSTR>(path.utf16()),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS,
            nullptr);

        if (hDir == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_SHARING_VIOLATION) {
                QThread::msleep(delay);
                delay = std::min(delay * 2, 200);
            } else {
                break;
            }
        }
    }

    return hDir;
}

void WindowsWorker::run() {
    emit started();

    QString currentPath;
    {
        QMutexLocker locker(&pathMutex);
        currentPath = pendingPath;
    }

    if (hDirectory.get() == INVALID_HANDLE_VALUE && !currentPath.isEmpty()) {
        HANDLE hDir = openDirectoryHandle(currentPath);
        if (hDir != INVALID_HANDLE_VALUE) {
            hDirectory = ScopedHandle(hDir);
            activeHandle.store(hDir, std::memory_order_release);
        }
    }

    if (hDirectory.get() == INVALID_HANDLE_VALUE) {
        emit finished();
        return;
    }

    constexpr DWORD notifyFilter =
        FILE_NOTIFY_CHANGE_FILE_NAME |
        FILE_NOTIFY_CHANGE_DIR_NAME |
        FILE_NOTIFY_CHANGE_ATTRIBUTES |
        FILE_NOTIFY_CHANGE_SIZE |
        FILE_NOTIFY_CHANGE_LAST_WRITE;

    while (true) {
        // ⭐ 优先检查运行状态（避免卡住）
        if (!isRunning.load(std::memory_order_acquire)) {
            break;
        }

        // ⭐ restart 逻辑
        if (needsRestart.exchange(false, std::memory_order_acquire)) {
            QString newPath;
            {
                QMutexLocker locker(&pathMutex);
                newPath = pendingPath;
            }

            // 先使原子句柄失效，避免 GUI 线程 cancel 到旧句柄
            activeHandle.store(INVALID_HANDLE_VALUE, std::memory_order_release);
            hDirectory.close();

            currentPath = newPath;
            HANDLE hDir = openDirectoryHandle(currentPath);
            if (hDir != INVALID_HANDLE_VALUE) {
                hDirectory = ScopedHandle(hDir);
                activeHandle.store(hDir, std::memory_order_release);
            }

            if (hDirectory.get() == INVALID_HANDLE_VALUE) {
                break;
            }
            continue;
        }

        // ⭐ 发起阻塞调用前复查 restart 标志，缩小 TOCTOU 窗口
        if (needsRestart.load(std::memory_order_acquire)) {
            continue;
        }

        DWORD bytesReturned = 0;

        if (!ReadDirectoryChangesW(
                hDirectory.get(),
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                FALSE,
                notifyFilter,
                &bytesReturned,
                nullptr,
                nullptr)) {

            const DWORD err = GetLastError();

            // ⭐ 被 cancel 且正在退出 → 直接结束
            if (!isRunning.load(std::memory_order_acquire)) {
                break;
            }

            // ⭐ 被 restart 打断
            if (needsRestart.load(std::memory_order_acquire)) {
                continue;
            }

            // ⭐ 事件包超过缓冲 → 指数扩容重试，而不是让 watcher 静默退出
            if (err == ERROR_NOTIFY_ENUM_DIR && buffer.size() < kMaxBufferSize) {
                buffer.resize(std::min(buffer.size() * 2, kMaxBufferSize));
                continue;
            }

            // 真错误
            break;
        }

        if (bytesReturned == 0) {
            continue;
        }

        auto notify = reinterpret_cast<PFILE_NOTIFY_INFORMATION>(buffer.data());
        QVector<NotifyEvent> batch;

        do {
            const auto len = notify->FileNameLength / sizeof(WCHAR);
            if (len > 0 && len <= kMaxFileNameLength) {
                const QString fileName(
                    reinterpret_cast<const QChar*>(notify->FileName),
                    static_cast<qsizetype>(len));

                batch.append(NotifyEvent{fileName, notify->Action});
            }

            if (notify->NextEntryOffset == 0) break;

            notify = reinterpret_cast<PFILE_NOTIFY_INFORMATION>(
                reinterpret_cast<std::byte*>(notify) + notify->NextEntryOffset);

        } while (true);

        // ⭐ 批量派发，减少排队信号与事件循环次数
        if (!batch.isEmpty()) {
            emit notifyEvents(batch);
        }
    }

    // ⭐ 确保 handle 关闭（避免悬挂）
    activeHandle.store(INVALID_HANDLE_VALUE, std::memory_order_release);
    hDirectory.close();

    emit finished();
}
