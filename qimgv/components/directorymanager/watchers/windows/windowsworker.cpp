// windowsworker.cpp
#include "windowsworker.h"
#include "windowswatcher_p.h"
#include <cstddef>
#include <QDir>
#include <QSet>
#include <QThread>

namespace {
constexpr qsizetype kInitialBufferSize = 131072;            // 128KB
constexpr qsizetype kMaxBufferSize = static_cast<qsizetype>(16) * 1024 * 1024;      // 16MB，防止无限膨胀
constexpr qsizetype kNetworkBufferSize = 65536;             // 64KB，网络目录(SMB)缓冲上限
constexpr qsizetype kMaxFileNameLength = 4096;              // 单条文件名上限（WCHAR）
constexpr int kShrinkQuietReads = 4;                        // 连续多少次低利用率读取后回收缓冲
}

WindowsWorker::WindowsWorker() {
    buffer.resize(kInitialBufferSize);
    readEvent.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    wakeEvent.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    overlapped.hEvent = readEvent.get();
}

void WindowsWorker::setRunning(bool running) {
    isRunning.store(running, std::memory_order_release);

    if (!running) {
        needsRestart.store(false, std::memory_order_release); // ⭐ 防止退出时误重启
        setReadyPath(QString());
        // 只唤醒 worker，由 worker 线程在 cleanup 中安全取消挂起读；
        // 避免 GUI 线程跨线程 CancelIoEx 与句柄关闭/复用产生竞态。
        if (wakeEvent)
            SetEvent(wakeEvent.get());
    }
}

void WindowsWorker::setWatchPath(const QString& path) {
    QMutexLocker locker(&pathMutex);
    pendingPath = path;
}

void WindowsWorker::requestDirectoryHandle(const QString& path) {
    {
        QMutexLocker locker(&pathMutex);
        // 路径没变且句柄有效 → 无需重启（监视仍保持）
        if (path == pendingPath &&
            activeHandle.load(std::memory_order_acquire) != INVALID_HANDLE_VALUE) {
            return;
        }
        pendingPath = path;
        // 请求一旦变化，旧路径的"就绪"立即失效：扫描必须等新 arm 完成
        readyPath.clear();
    }
    needsRestart.store(true, std::memory_order_release);
    // 只唤醒 worker：旧读由 worker 线程自己取消，避免 GUI 线程的 CancelIoEx
    // 在新句柄复用同一句柄值后误取消刚挂上的新读。
    if (wakeEvent)
        SetEvent(wakeEvent.get());
}

bool WindowsWorker::isWatchPathReady(const QString& path) const {
    QMutexLocker locker(&pathMutex);
    if (readyPath.isEmpty())
        return false;
    // Windows 路径大小写不敏感，且允许尾部分隔符差异
    return QDir::cleanPath(readyPath).compare(
               QDir::cleanPath(path), Qt::CaseInsensitive) == 0;
}

void WindowsWorker::setReadyPath(const QString& path) {
    QMutexLocker locker(&pathMutex);
    readyPath = path;
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
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
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

bool WindowsWorker::ensureInitialHandle(QString &currentPath) {
    {
        QMutexLocker locker(&pathMutex);
        currentPath = pendingPath;
    }
    if(hDirectory.get() == INVALID_HANDLE_VALUE && !currentPath.isEmpty()) {
        HANDLE hDir = openDirectoryHandle(currentPath);
        if(hDir != INVALID_HANDLE_VALUE) {
            hDirectory = ScopedHandle(hDir);
            activeHandle.store(hDir, std::memory_order_release);
        }
    }
    return hDirectory.get() != INVALID_HANDLE_VALUE;
}

bool WindowsWorker::handleRestart(QString &currentPath, int &quietReads) {
    QString newPath;
    {
        QMutexLocker locker(&pathMutex);
        newPath = pendingPath;
    }
    // 先撤销就绪状态并使原子句柄失效，避免 GUI 线程把旧句柄当成就绪
    setReadyPath(QString());
    activeHandle.store(INVALID_HANDLE_VALUE, std::memory_order_release);
    hDirectory.close();
    currentPath = std::move(newPath);
    HANDLE hDir = openDirectoryHandle(currentPath);
    if(hDir != INVALID_HANDLE_VALUE) {
        hDirectory = ScopedHandle(hDir);
        activeHandle.store(hDir, std::memory_order_release);
    }
    // ⭐ 新句柄/新路径，网络上限标记需重新判定，缓冲回收计数也复位
    networkBufferCapped = false;
    quietReads = 0;
    return hDirectory.get() != INVALID_HANDLE_VALUE;
}

bool WindowsWorker::armRead(const QString& directoryPath) {
    if (hDirectory.get() == INVALID_HANDLE_VALUE || overlapped.hEvent == nullptr)
        return false;

    constexpr DWORD notifyFilter =
        FILE_NOTIFY_CHANGE_FILE_NAME |
        FILE_NOTIFY_CHANGE_DIR_NAME |
        FILE_NOTIFY_CHANGE_ATTRIBUTES |
        FILE_NOTIFY_CHANGE_SIZE |
        FILE_NOTIFY_CHANGE_LAST_WRITE;

    while (isRunning.load(std::memory_order_acquire)) {
        ResetEvent(overlapped.hEvent);
        const BOOL ok = ReadDirectoryChangesW(
            hDirectory.get(), buffer.data(), static_cast<DWORD>(buffer.size()), FALSE,
            notifyFilter, nullptr, &overlapped, nullptr);
        if (ok) {
            readPending = true;
            // stop 期间成功挂上的读不再发布就绪，由 cleanup 统一取消；
            // 仍返回 true，避免把正常停止误报成 watchPathFailed。
            if (!isRunning.load(std::memory_order_acquire))
                return true;
            if (announceReady) {
                announceReady = false;
                // 先发布就绪状态再发信号：查询与信号两条路径都不会看到假阴性
                setReadyPath(directoryPath);
                emit watchPathReady(directoryPath);
            }
            return true;
        }

        const DWORD err = GetLastError();
        // ⭐ 网络目录(SMB)缓冲 >64KB 触发 ERROR_INVALID_PARAMETER → 降档至 64KB
        if (err == ERROR_INVALID_PARAMETER && buffer.size() > kNetworkBufferSize) {
            buffer.resize(kNetworkBufferSize);
            networkBufferCapped = true;
            continue;
        }
        return false;
    }
    return false;
}

void WindowsWorker::cancelPendingRead() {
    if (!readPending)
        return;

    HANDLE hDir = hDirectory.get();
    if (hDir != INVALID_HANDLE_VALUE && overlapped.hEvent != nullptr) {
        CancelIoEx(hDir, &overlapped);
        DWORD ignored = 0;
        // 等内核收回缓冲区所有权后再关闭/复用 buffer
        GetOverlappedResult(hDir, &overlapped, &ignored, TRUE);
    }
    readPending = false;
}

WindowsWorker::WaitResult WindowsWorker::waitOnce(DWORD &bytesReturned) {
    bytesReturned = 0;
    if (!readPending)
        return WaitResult::Retry;

    constexpr DWORD kWaitHandleCount = 2;
    HANDLE waitHandles[kWaitHandleCount] = { overlapped.hEvent, wakeEvent.get() };
    const DWORD waitResult =
        WaitForMultipleObjects(kWaitHandleCount, waitHandles, FALSE, INFINITE);
    if (waitResult == WAIT_OBJECT_0 + 1) {
        ResetEvent(wakeEvent.get());
        return WaitResult::Retry;
    }
    if (waitResult != WAIT_OBJECT_0)
        return WaitResult::Stop;

    readPending = false;
    if (!GetOverlappedResult(hDirectory.get(), &overlapped, &bytesReturned, FALSE)) {
        const DWORD err = GetLastError();
        // ⭐ 被 cancel 且正在退出 → 直接结束
        if (!isRunning.load(std::memory_order_acquire))
            return WaitResult::Stop;
        // ⭐ 被 restart 打断（worker 自己 CancelIoEx）或异常 abort → 重挂
        if (needsRestart.load(std::memory_order_acquire) || err == ERROR_OPERATION_ABORTED)
            return WaitResult::Retry;
        // ⭐ 系统内部缓冲溢出（事件已丢失，扩容应用缓冲无效）→ 直接重挂
        if (err == ERROR_NOTIFY_ENUM_DIR)
            return WaitResult::Retry;
        // ⭐ 网络目录(SMB)在完成阶段报缓冲过大：降档后重挂
        if (err == ERROR_INVALID_PARAMETER && buffer.size() > kNetworkBufferSize) {
            buffer.resize(kNetworkBufferSize);
            networkBufferCapped = true;
            return WaitResult::Retry;
        }
        return WaitResult::Stop;
    }
    return WaitResult::Ok;
}

QVector<NotifyEvent> WindowsWorker::parseBatch(DWORD bytesReturned, const QString &directoryPath) {
    auto notify = reinterpret_cast<PFILE_NOTIFY_INFORMATION>(buffer.data());
    QVector<NotifyEvent> batch;
    batch.reserve(bytesReturned / 32 + 16);
    do {
        const auto len = notify->FileNameLength / sizeof(WCHAR);
        if(len > 0 && len <= kMaxFileNameLength) {
            const QString fileName(QString::fromUtf16(
                reinterpret_cast<const char16_t*>(notify->FileName),
                static_cast<qsizetype>(len)));
            batch.append(NotifyEvent{directoryPath, fileName, notify->Action});
        }
        if(notify->NextEntryOffset == 0)
            break;
        notify = reinterpret_cast<PFILE_NOTIFY_INFORMATION>(
            reinterpret_cast<std::byte*>(notify) + notify->NextEntryOffset);
    } while(true);
    return batch;
}

void WindowsWorker::dedupModified(QVector<NotifyEvent> &batch) {
    // ⭐ 批内去重：Windows 一次保存常同时产生 SIZE/LAST_WRITE 两个 MODIFY
    if(batch.size() <= 1)
        return;
    QSet<QString> seenModified;
    QVector<NotifyEvent> deduped;
    deduped.reserve(batch.size());
    for(const NotifyEvent& ev : batch) {
        if(ev.action == FILE_ACTION_MODIFIED) {
            if(seenModified.contains(ev.fileName))
                continue;
            seenModified.insert(ev.fileName);
        }
        deduped.append(ev);
    }
    if(deduped.size() != batch.size())
        batch = std::move(deduped);
}

void WindowsWorker::emitAndMaybeShrink(const QVector<NotifyEvent> &batch, DWORD bytesReturned, int &quietReads) {
    // ⭐ 批量派发，减少排队信号与事件循环次数
    if(!batch.isEmpty())
        emit notifyEvents(batch);
    // ⭐ 扩容仅用于应急；突发结束后再回收缓冲，避免常驻大内存
    if(buffer.size() > kInitialBufferSize) {
        if(static_cast<qsizetype>(bytesReturned) < buffer.size() / 2)
            ++quietReads;
        else
            quietReads = 0;
        if(quietReads >= kShrinkQuietReads) {
            buffer.resize(kInitialBufferSize);
            quietReads = 0;
        }
    }
}

void WindowsWorker::run() {
    emit started();
    announceReady = true;
    readPending = false;
    // 上一次 run 若是网络目录，64KB 上限不能带给后续本地目录
    networkBufferCapped = false;

    QString currentPath;
    bool healthy = ensureInitialHandle(currentPath) && armRead(currentPath);
    if (!healthy) {
        setReadyPath(QString());
        if (isRunning.load(std::memory_order_acquire))
            emit watchPathFailed(currentPath);
    }

    // ⭐ 缓冲回收前的连续低利用率读取计数（仅 worker 线程访问）
    int quietReads = 0;

    while(healthy && isRunning.load(std::memory_order_acquire)) {
        // ⭐ restart 逻辑
        if(needsRestart.exchange(false, std::memory_order_acquire)) {
            QString pending;
            {
                QMutexLocker locker(&pathMutex);
                pending = pendingPath;
            }
            // 只有「当前读仍挂着且仍处于就绪状态」时，同路径请求才算已满足。
            // ready 被中途路径请求清掉时必须重开并重新 arm，不能凭 readPending
            // 直接恢复，否则扫描可能落在“旧读已完成、下一次读尚未挂上”的缝里。
            if(pending == currentPath && readPending && isWatchPathReady(currentPath))
                continue;

            cancelPendingRead();
            if(!handleRestart(currentPath, quietReads)) {
                setReadyPath(QString());
                if (isRunning.load(std::memory_order_acquire))
                    emit watchPathFailed(currentPath);
                healthy = false;
                break;
            }
            announceReady = true;
            if(!armRead(currentPath)) {
                setReadyPath(QString());
                if (isRunning.load(std::memory_order_acquire))
                    emit watchPathFailed(currentPath);
                healthy = false;
                break;
            }
            continue;
        }

        // 上一轮读取已完成/被取消：重新挂上监视后再等待
        if(!readPending) {
            if(!armRead(currentPath)) {
                setReadyPath(QString());
                if (isRunning.load(std::memory_order_acquire))
                    emit watchPathFailed(currentPath);
                healthy = false;
                break;
            }
        }

        DWORD bytesReturned = 0;
        const WaitResult wr = waitOnce(bytesReturned);
        if(wr == WaitResult::Stop) {
            // 非正常停止（等待失败/读取错误）也要通知等待扫描的一方，
            // 否则目录加载要多等一次 500ms 超时兜底。
            setReadyPath(QString());
            if (isRunning.load(std::memory_order_acquire))
                emit watchPathFailed(currentPath);
            break;
        }
        if(wr == WaitResult::Retry)
            continue;

        if(bytesReturned == 0) {
            // ⭐ 应用缓冲太小，整批事件被丢弃 → 指数扩容后重试（网络目录上限 64KB）
            const qsizetype limit = networkBufferCapped ? kNetworkBufferSize : kMaxBufferSize;
            if(buffer.size() < limit) {
                buffer.resize(std::min(buffer.size() * 2, limit));
                quietReads = 0;
            }
            continue;
        }

        QVector<NotifyEvent> batch = parseBatch(bytesReturned, currentPath);
        dedupModified(batch);
        emitAndMaybeShrink(batch, bytesReturned, quietReads);
    }

    // ⭐ 确保 handle 关闭（避免悬挂）
    cancelPendingRead();
    setReadyPath(QString());
    activeHandle.store(INVALID_HANDLE_VALUE, std::memory_order_release);
    hDirectory.close();

    emit finished();
}
