// directorywatcher.h
#pragma once

#include <QObject>
#include <QString>

class DirectoryWatcherPrivate;

class DirectoryWatcher : public QObject {
    Q_OBJECT
public:
    static DirectoryWatcher* newInstance();
    virtual ~DirectoryWatcher();

    virtual void setWatchPath(const QString& watchPath);
    virtual QString watchPath() const;
    bool isObserving() const;
    // 目标目录是否已经真正挂上监视（Windows 上指 ReadDirectoryChangesW 已成功发起）。
    // 扫描必须等到它返回 true 后再开始，否则切换目录的交接窗口内可能漏事件。
    virtual bool isWatchPathReady(const QString& path) const;

public Q_SLOTS:
    virtual void observe();
    virtual void stopObserving();
    virtual void requestWatchPath(const QString& path);

Q_SIGNALS:
    // ⭐ 事件必须携带产生它的目录：平滑切换监视路径时，旧目录的排队事件
    // 可能在新路径生效后才派发，仅凭文件名会被错误地归到新目录。
    void fileCreated(const QString& directoryPath, const QString& fileName);
    void fileDeleted(const QString& directoryPath, const QString& fileName);
    void fileRenamed(const QString& directoryPath, const QString& oldName, const QString& newName);
    void fileModified(const QString& directoryPath, const QString& fileName);
    // 监视已真正生效；DirectoryManager 收到后才启动目录扫描
    void watchPathReady(const QString& directoryPath);
    // 无法建立监视（目录不可打开/句柄失效）；扫描仍需继续，只是没有自动刷新
    void watchPathFailed(const QString& directoryPath);
    void observingStarted();
    void observingStopped();

protected:
    explicit DirectoryWatcher(DirectoryWatcherPrivate* ptr);
    DirectoryWatcherPrivate* d_ptr;

private:
    Q_DECLARE_PRIVATE(DirectoryWatcher)
    Q_DISABLE_COPY_MOVE(DirectoryWatcher)
};