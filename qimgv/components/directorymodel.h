#pragma once
#include <QObject>
#include "cache/cache.h"
#include "directorymanager/directorymanager.h"
#include "scaler/scaler.h"
#include "loader/loader.h"
#include "utils/fileoperations.h"

class DirectoryModel : public QObject {
    Q_OBJECT
public:
    explicit DirectoryModel(QObject *parent = nullptr);
    ~DirectoryModel();
    Scaler *scaler;
    void load(const QString &filePath, bool asyncHint);
    void preload(const QString &filePath);
    int fileCount() const;
    int dirCount() const;
    int indexOfFile(const QString &filePath) const;
    int indexOfDir(const QString &filePath) const;
    const QString &fileNameAt(int index) const;
    bool containsFile(const QString &filePath) const;
    bool isEmpty() const;
    // 目录枚举在 worker 线程进行：true 表示当前目录列表尚未就绪（loaded 未发出）
    bool isScanning() const;
    const QString &nextOf(const QString &filePath) const;
    const QString &prevOf(const QString &filePath) const;
    const QString &firstFile() const;
    const QString &lastFile() const;
    bool forceInsert(const QString &filePath);
    // 后缀校验后增量插入（用于显式打开不在当前列表中的文件）；内部会 emit fileAdded
    bool insertFile(const QString &filePath);
    void copyFileTo(const QString &srcFile, const QString &destDirPath, bool force, FileOpResult &result);
    void moveFileTo(const QString &srcFile, const QString &destDirPath, bool force, FileOpResult &result);
    void renameEntry(const QString &oldFilePath, const QString &newName, bool force, FileOpResult &result);
    void removeFile(const QString &filePath, bool trash, FileOpResult &result);
    void removeDir(const QString &dirPath, bool trash, bool recursive, FileOpResult &result);
    bool setDirectory(const QString &path);
    void unload(int index);
    bool loaderBusy() const;
    std::shared_ptr<Image> getImageAt(int index);
    std::shared_ptr<Image> getImage(const QString &filePath);
    // 仅查缓存，绝不触发解码（GUI 线程 resize/滚轮等高频路径使用）
    std::shared_ptr<Image> cachedImage(const QString &filePath);
    void updateImage(const QString &filePath, const std::shared_ptr<Image> &img);
    void setSortingMode(SortingMode mode);
    SortingMode sortingMode() const;
    // 目录扫描完成后把图片缓存的键从用户传入路径改为枚举路径（Core::onModelLoaded），
    // 否则按当前路径做的 isLoaded()/cachedImage() 精确查找会全部 miss。
    // 新键已被同一对象占用时合并旧键（同一文件两种拼写不会留下两份条目）；
    // 返回 false 表示条目尚不存在或新键被另一对象占用，调用方按需取舍
    bool rekey(const QString &oldPath, const QString &newPath);
    const QString directoryPath() const;
    void unload(const QString &filePath);
    bool isLoaded(int index) const;
    bool isLoaded(const QString& filePath) const;
    void reload(const QString &filePath);
    const QString &filePathAt(int index) const;
    const FSEntry &fileEntryAt(int index) const;
    size_t totalCount() const;
    const QString &dirNameAt(int index) const;
    const QString &dirPathAt(int index) const;
    bool autoRefresh();
    bool saveFile(const QString &filePath);
    bool saveFile(const QString &filePath, const QString &destPath);
    bool containsDir(const QString &dirPath) const;

signals:
    void fileRemoved(const QString &filePath, int index);
    void fileRenamed(const QString &fromPath, int indexFrom, const QString &toPath, int indexTo);
    void fileAdded(QString filePath);
    void fileModified(QString filePath);
    void loaded(QString filePath);
    void loadFailed(const QString &path);
    void sortingChanged(SortingMode);
    void indexChanged(int oldIndex, int index);
    void imageReady(std::shared_ptr<Image> img, const QString&);
    void imageUpdated(QString filePath);

private:
    DirectoryManager dirManager;
    Loader loader;
    Cache cache;

private slots:
    void onImageReady(const std::shared_ptr<Image> &img, const QString &path);
    void onSortingChanged();
    void onFileAdded(const QString &filePath);
    void onFileRemoved(const QString &filePath, int index);
    void onFileRenamed(const QString &fromPath, int indexFrom, const QString &toPath, int indexTo);
    void onFileModified(const QString &filePath);
};