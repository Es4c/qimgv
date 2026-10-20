#pragma once

#include <QObject>
#include <QString>
#include <QDir>
#include <QFileInfo>
#include <QCollator>
#include <QDateTime>
#include <QSet>
#include <QThreadPool>
#include <QTimer>
#include <QVector>
#include <QPair>
#include <array>
#include <atomic>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <ranges>
#include <filesystem>
#include "settings.h"
#include "watchers/directorywatcher.h"
#include "utils/stuff.h"
#include "sourcecontainers/fsentry.h"

#ifdef Q_OS_WIN32
#include "windows.h"
#endif

enum FileListSource {
    // rename? wip
    SOURCE_DIRECTORY,
    SOURCE_DIRECTORY_RECURSIVE,
    // 仅枚举子目录的轻量列表（相邻目录切换用），目录路径仍然有效
    SOURCE_DIRECTORY_DIRS_ONLY
};

class DirectoryManager;

// 支持格式后缀匹配器：按长度分桶，逐文件只需与同长度的少量后缀比较
//（不同长度必然不等）。值语义、不读 Settings，可在 worker 线程持有一份快照。
class SuffixMatcher {
public:
    SuffixMatcher() = default;
    explicit SuffixMatcher(const QList<QByteArray> &formats);

    bool matches(const QStringView &suffix) const;

private:
    static constexpr int kMaxBuiltinSuffixLen = 16;
    std::array<std::vector<QString>, kMaxBuiltinSuffixLen> mBuckets; // 下标 = 长度-1
    std::vector<QString> mLongBucket;                                // 超长后缀兜底
};

// 类内定义类型别名，修复编译错误
class DirectoryManager : public QObject {
    Q_OBJECT
public:
    // 类型别名 - 类内定义（修复编译错误关键）
    using CompareFunction = bool (DirectoryManager::*)(const FSEntry &e1, const FSEntry &e2) const;

    DirectoryManager();
    ~DirectoryManager() override;

    // ignored if the same dir is already opened
    bool setDirectory(const QString &dirPath);
    bool setDirectoryRecursive(const QString &dirPath);
    // 仅加载子目录名（相邻目录切换用）：跳过文件 stat/后缀/文件排序，不启动 watcher、不 emit loaded
    bool setDirectoryDirsOnly(const QString &dirPath);

    // 目录枚举在 worker 线程进行：true 表示当前目录列表还在扫描（尚未 emit loaded）
    inline bool isScanning() const { return mScanInProgress; }

    // 内联优化：移至头文件
    inline QString directoryPath() const {
        if(mListSource == SOURCE_DIRECTORY || mListSource == SOURCE_DIRECTORY_RECURSIVE ||
           mListSource == SOURCE_DIRECTORY_DIRS_ONLY)
            return mDirectoryPath;
        return "";
    }

    int indexOfFile(const QString &filePath) const;
    int indexOfDir(const QString &dirPath) const;
    const QString &filePathAt(int index) const;
    
    // 内联优化：移至头文件
    inline unsigned long fileCount() const { return fileEntryVec.size(); }
    inline unsigned long dirCount() const { return dirEntryVec.size(); }
    inline unsigned long totalCount() const { return fileCount() + dirCount(); }

    inline bool isEmpty() const { return fileEntryVec.empty(); }

    // 内联优化：contains 方法使用哈希表查找 O(1)
    inline bool containsFile(const QString &filePath) const {
        return mFileIndexMap.contains(filePath);
    }

    inline bool containsDir(const QString &dirPath) const {
        return mDirIndexMap.contains(dirPath);
    }

    const QString &fileNameAt(int index) const;
    const QString &prevOfFile(const QString &filePath) const;
    const QString &nextOfFile(const QString &filePath) const;
    const QString &prevOfDir(const QString &dirPath) const;
    const QString &nextOfDir(const QString &dirPath) const;
    void sortEntryLists();
    const QString &firstFile() const;
    const QString &lastFile() const;
    void setSortingMode(SortingMode mode);
    
    // 内联优化：移至头文件
    inline SortingMode sortingMode() const { return mSortingMode; }

    bool isFile(const QString &path) const;
    bool isDir(const QString &path) const;
    const FSEntry &fileEntryAt(int index) const;
    const QString &dirPathAt(int index) const;
    const QString &dirNameAt(int index) const;
    
    // 内联优化：移至头文件
    inline bool fileWatcherActive() {
        if(!watcher) return false;
        return watcher->isObserving();
    }

    bool insertFileEntry(const QString &filePath);
    bool forceInsertFileEntry(const QString &filePath);
    bool forceInsertFileEntry(const QString &filePath, const FSEntry &entry);
    void removeFileEntry(const QString &filePath);
    void updateFileEntry(const QString &filePath);
    // ⭐ 复用已 stat 的 QFileInfo，避免 exists()+构造两次 stat
    void updateFileEntry(const QString &filePath, const QFileInfo &fi);
    void renameFileEntry(const FilePath &oldFilePath, const FileName &newFileName);
    bool insertDirEntry(const QString &dirPath);
    void removeDirEntry(const QString &dirPath);
    void renameDirEntry(const DirPath &oldDirPath, const DirName &newDirName);

    QStringList fileList() const;

    // watcher events guard to avoid race when DirectoryModel does local rename/move/remove
    inline void setIgnoreWatcherEvents(bool ignore) { mIgnoreWatcherEvents = ignore; }
    inline bool ignoreWatcherEvents() const { return mIgnoreWatcherEvents; }

private:
    // 增量排序优化：记录上次排序的比较函数，避免不必要的重新排序
    CompareFunction mLastCompareFunction = nullptr;
    bool mFilesSorted = false;
    bool mDirsSorted = false;

    // 增量排序辅助方法
    void sortFileEntryListsIncremental();
    void sortDirEntryListsIncremental();

    // 哈希索引映射 - 性能优化核心 (O(n) → O(1))
    std::unordered_map<QString, int> mFileIndexMap;
    std::unordered_map<QString, int> mDirIndexMap;

    // 索引映射维护方法
    void rebuildFileIndexMap();
    void rebuildDirIndexMap();
    void updateFileIndexAfterInsert(const QString &path, int index);
    void updateFileIndexAfterRemove(const QString &path, int index);
    void updateDirIndexAfterInsert(const QString &path, int index);
    void updateDirIndexAfterRemove(const QString &path, int index);
    // 重命名只影响 [first, last] 区间：只刷新这段映射，避免整表 rebuild
    void refreshFileIndexRange(int first, int last);
    void refreshDirIndexRange(int first, int last);

    // watcher 事件批量处理：去重 + 合并，避免每个事件单独 O(n) 索引重写
    void flushPendingEvents();
    void clearPendingEvents();
    void processPendingRenames(const QVector<QPair<QString, QString>> &renames);
    void processPendingRemovals(const QVector<QString> &removes);
    void processPendingAdditions(const QVector<QString> &adds);
    void processPendingModifications(const QSet<QString> &modifies);

    // ⭐ 批量重命名内部实现：完成 vector 操作并按影响区间更新索引映射，不 emit，
    // 由调用方按条 emit（链式 rename 依赖每步新鲜索引）。
    // 文件重命名还需 emit；目录重命名只需索引映射。
    struct FileRenameEmit { QString from; int fromIndex; QString to; int toIndex; };
    bool renameFileEntryBatch(const QString &oldPath, const QString &newName,
                              FileRenameEmit &emitOut, QVector<QPair<QString,int>> &extraRemoves,
                              QVector<QString> &extraAdds, QVector<QString> &extraRefresh);
    bool renameDirEntryBatch(const QString &oldPath, const QString &newName);

private:
    // ⭐ 后缀按长度分桶：目录扫描时每个文件只与同长度的少量后缀做
    // 大小写不敏感比较（不同长度必然不等），替代对全量集合的线性扫描。
    // readSettings() 一次性重建，worker 侧复制一份快照
    SuffixMatcher mSuffixMatcher;
    QCollator collator;
    std::vector<FSEntry> fileEntryVec, dirEntryVec;
    const FSEntry defaultEntry;
    QString mDirectoryPath;
    DirectoryWatcher* watcher = nullptr;
    SortingMode mSortingMode = SORT_NAME;
    FileListSource mListSource = SOURCE_DIRECTORY;
    // ⭐ 目录枚举在 worker 上跑：GUI 线程只接收已排好序的结果并建索引。
    // mScanGeneration 用来取消过期扫描（切目录/新请求时递增），
    // mScanInProgress 期间到达的 watcher 事件先缓冲，扫描结果应用后重放，
    // 避免扫描耗时期间的文件变化被永久丢弃
    // 代数先于线程池声明：析构顺序相反，保证池内残留任务读取代数时它仍然有效
    std::atomic<quint64> mScanGeneration{0};
    QThreadPool mScanPool;
    bool mScanInProgress = false;
    // setDirectoryDirsOnly 复用缓存有效性：父目录 mtime + 排序设置（排序模式/文件夹排序）
    // 全部一致才跳过重扫。next/prevDirectory 每次按键都会走到这里
    QDateTime mDirsOnlyMtime;
    SortingMode mDirsOnlySortingMode = SORT_NAME;
    bool mDirsOnlySortFolders = false;
    // 替代 static const QString emptyString - 线程安全且性能更好
    QString mEmptyString;
    bool mIgnoreWatcherEvents = false;

    // 批量事件缓冲：0ms 单次定时器合并同一轮事件循环内的 watcher 事件
    // 单条有序队列保留真实到达顺序，供合并时 last-op-wins 与 rename 链判定
    struct PendingEvent {
        enum class Type : quint8 { Add, Remove, Modify, Rename };
        Type type;
        QString path;     // Rename 时为旧路径，其余为完整路径
        QString newName;  // 仅 Rename 使用：新文件名
    };
    QTimer mEventBatchTimer;
    QVector<PendingEvent> mPendingEvents;

    // 扫描启动与 watcher 路径交接的顺序保证：
    // 等待目标目录真正 arm 后再投递扫描；watcher 卡死/失败才走超时兜底。
    QTimer mWatchReadyTimer;
    QString mPendingScanPath;
    quint64 mPendingScanGeneration = 0;
    bool mPendingScanRecursive = false;
    bool mScanStartPending = false;
    // 超时/失败兜底扫描开始时 watcher 尚未 arm；若之后 arm，需要补一次扫描关闭交接窗口
    bool mScanStartedWithoutWatch = false;

    void readSettings();
    // watcher 事件入口：只接受当前监视目录的事件；扫描期间先入队，结束后重放
    bool acceptsWatcherEvent(const QString &directoryPath) const;
    void queueWatcherEvent(PendingEvent ev);
    void loadEntryListAsync(const QString &directoryPath, bool recursive,
                            bool preservePendingEvents = false);
    // watcher 就绪后再投递扫描：目录切换的 arm 窗口由就绪握手覆盖。
    // 返回 true 表示本次调用确实投递了扫描。
    bool startPendingScan();
    void onWatchReadyTimeout();
    void cancelPendingScan();
    void dispatchScan(const QString &directoryPath, bool recursive, quint64 generation);
    void applyScanResult(std::vector<FSEntry> files, std::vector<FSEntry> dirs,
                         quint64 generation, const QString &path,
                         SortingMode scannedSortMode, bool scannedSortFolders);
    void clearEntryListData();
    void clearEntryList();
    bool path_entry_compare(const FSEntry &e1, const FSEntry &e2) const;
    bool path_entry_compare_reverse(const FSEntry &e1, const FSEntry &e2) const;
    bool name_entry_compare(const FSEntry &e1, const FSEntry &e2) const;
    bool name_entry_compare_reverse(const FSEntry &e1, const FSEntry &e2) const;
    bool date_entry_compare(const FSEntry &e1, const FSEntry &e2) const;
    bool date_entry_compare_reverse(const FSEntry &e1, const FSEntry &e2) const;
    auto compareFunction() -> CompareFunction;
    // 目录插入/重命名使用的比较器：sortFolders 关闭时目录始终按路径升序，
    // 与 sortEntries(byPath=true) / setDirectoryDirsOnly 的整表排序保持一致
    auto dirCompareFunction() -> CompareFunction;
    bool size_entry_compare(const FSEntry &e1, const FSEntry &e2) const;
    bool size_entry_compare_reverse(const FSEntry &e1, const FSEntry &e2) const;
    void startFileWatcher(const QString &directoryPath);
    void stopFileWatcher();
    void addDirEntriesOnly(std::vector<FSEntry> &entryVec, const QString &directoryPath);

    // 内联优化：范围检查函数移至头文件
    inline bool checkFileRange(int index) const {
        return index >= 0 && index < static_cast<int>(fileEntryVec.size());
    }

    inline bool checkDirRange(int index) const {
        return index >= 0 && index < static_cast<int>(dirEntryVec.size());
    }

private slots:
    void onFileAddedExternal(const QString &directoryPath, const QString &fileName);
    void onFileRemovedExternal(const QString &directoryPath, const QString &fileName);
    void onFileModifiedExternal(const QString &directoryPath, const QString &fileName);
    void onFileRenamedExternal(const QString &directoryPath, const QString &oldFileName,
                               const QString &newFileName);
    void onWatchPathReady(const QString &directoryPath);
    void onWatchPathFailed(const QString &directoryPath);
    void onWatchPathOverflowed(const QString &directoryPath);

signals:
    void loaded(const QString &path);
    void sortingChanged();
    void fileRemoved(QString filePath, int);
    void fileModified(QString filePath);
    void fileAdded(QString filePath);
    void fileRenamed(QString fromPath, int indexFrom, QString toPath, int indexTo);
};
