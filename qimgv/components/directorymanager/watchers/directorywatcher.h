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

public Q_SLOTS:
    void observe();
    void stopObserving();
    void requestWatchPath(const QString& path);

Q_SIGNALS:
    // ⭐ 事件必须携带产生它的目录：平滑切换监视路径时，旧目录的排队事件
    // 可能在新路径生效后才派发，仅凭文件名会被错误地归到新目录。
    void fileCreated(const QString& directoryPath, const QString& fileName);
    void fileDeleted(const QString& directoryPath, const QString& fileName);
    void fileRenamed(const QString& directoryPath, const QString& oldName, const QString& newName);
    void fileModified(const QString& directoryPath, const QString& fileName);
    void observingStarted();
    void observingStopped();

protected:
    explicit DirectoryWatcher(DirectoryWatcherPrivate* ptr);
    DirectoryWatcherPrivate* d_ptr;

private:
    Q_DECLARE_PRIVATE(DirectoryWatcher)
    Q_DISABLE_COPY_MOVE(DirectoryWatcher)
};