#include "directorymanager.h"
#include "utils/pathutils.h"
#include <QDebug>
#include <QDir>
#include <QHash>
#include <QRunnable>
#include <cstddef>
#include <iterator>
#include <utility>

namespace fs = std::filesystem;

// watcher 未能及时 arm 时不能永久扣住目录加载：超时后先普通扫描，
// 之后若 arm 成功再由 onWatchPathReady() 补扫，关闭交接窗口。
// 正常本地目录在提交线程后几毫秒内就会发出 watchPathReady，不会走到这里。
constexpr int kWatchReadyTimeoutMs = 500;

// ==================== SuffixMatcher ====================

SuffixMatcher::SuffixMatcher(const QList<QByteArray> &formats) {
    constexpr std::size_t builtinBuckets =
        static_cast<std::size_t>(kMaxBuiltinSuffixLen);
    for (const auto &fmt : formats) {
        QString suffix = QString::fromLatin1(fmt).toLower();
        const std::size_t len = static_cast<std::size_t>(suffix.size());
        if (len == 0)
            continue;
        if (len <= builtinBuckets)
            mBuckets[len - 1].push_back(std::move(suffix));
        else
            mLongBucket.push_back(std::move(suffix));
    }
}

bool SuffixMatcher::matches(const QStringView &suffix) const {
    constexpr std::size_t builtinBuckets =
        static_cast<std::size_t>(kMaxBuiltinSuffixLen);
    const std::size_t len = static_cast<std::size_t>(suffix.size());
    if (len == 0)
        return false;

    const std::vector<QString> &bucket =
        (len <= builtinBuckets) ? mBuckets[len - 1] : mLongBucket;

    for (const QString &s : bucket) {
        if (suffix.compare(s, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

DirectoryManager::DirectoryManager() {
    // 关键修改：显式设置区域设置为系统默认，确保 NumericMode 生效
    collator.setLocale(QLocale::system());
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    readSettings();
    setSortingMode(settings->sortingMode());
    connect(settings, &Settings::settingsChanged, this, &DirectoryManager::readSettings);
    // 批量事件：0ms 单次定时器，合并同一轮事件循环内到达的 watcher 事件
    mEventBatchTimer.setSingleShot(true);
    mEventBatchTimer.setInterval(0);
    connect(&mEventBatchTimer, &QTimer::timeout, this, &DirectoryManager::flushPendingEvents);
    // watcher arm 握手超时：只影响自动刷新，不能阻塞目录加载
    mWatchReadyTimer.setSingleShot(true);
    mWatchReadyTimer.setInterval(kWatchReadyTimeoutMs);
    connect(&mWatchReadyTimer, &QTimer::timeout,
            this, &DirectoryManager::onWatchReadyTimeout);
    // 目录扫描单线程：同一时刻只会有一个当前目录，串行扫描即可；
    // 切目录时旧任务按代数自检放弃，不会与新扫描抢磁盘
    mScanPool.setMaxThreadCount(1);
    // mEmptyString 默认构造即为空字符串，无需额外初始化
}

DirectoryManager::~DirectoryManager() {
    // 取消在跑的扫描再等待：扫描循环每 256 个条目检查一次代数，不会长时间卡住退出
    mScanGeneration.fetch_add(1, std::memory_order_relaxed);
    cancelPendingScan();
    mScanPool.waitForDone();
    // watcher 无 parent，需手动停止并释放，避免临时对象泄漏线程
    stopFileWatcher();
    delete watcher;
}

namespace {
// 目录枚举结果（worker 产出，GUI 线程接收）
struct ScannedEntries {
    std::vector<FSEntry> files;
    std::vector<FSEntry> dirs;
    bool cancelled = false;
};

QCollator makeCollator() {
    QCollator collator(QLocale::system());
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    return collator;
}

// 统一的排序实现：名称/路径排序预计算 sortKey（每条目一次 locale 调用），
// 时间/大小排序直接比较。byPath 表示"始终按路径升序"（目录不跟文件夹排序时）。
void sortEntries(std::vector<FSEntry> &entries, SortingMode mode, bool byPath, QCollator &collator) {
    if (entries.size() < 2)
        return;

    const bool byName = !byPath && (mode == SORT_NAME || mode == SORT_NAME_DESC);
    if (byName || byPath) {
        const bool reverse = byName && mode == SORT_NAME_DESC;
        std::vector<QCollatorSortKey> keys;
        keys.reserve(entries.size());
        for (const auto &e : entries)
            keys.push_back(collator.sortKey(byPath ? e.path : e.name));
        std::vector<size_t> order(entries.size());
        for (size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        std::ranges::sort(order, [&](size_t a, size_t b) {
            return reverse ? keys[b] < keys[a] : keys[a] < keys[b];
        });
        std::vector<FSEntry> sorted;
        sorted.reserve(entries.size());
        for (size_t idx : order)
            sorted.push_back(std::move(entries[idx]));
        entries = std::move(sorted);
        return;
    }

    std::ranges::sort(entries, [mode, &collator](const FSEntry &a, const FSEntry &b) {
        switch (mode) {
        case SORT_TIME:      return a.modifyTime < b.modifyTime;
        case SORT_TIME_DESC: return a.modifyTime > b.modifyTime;
        case SORT_SIZE:      return a.size < b.size;
        case SORT_SIZE_DESC: return a.size > b.size;
        default:             return collator.compare(a.name, b.name) < 0;
        }
    });
}

// 与目录扫描的原内联判断一致：取 basename 最后一个点之后的部分
bool hasSupportedSuffix(const QString &name, const SuffixMatcher &matcher) {
    const qsizetype dot = name.lastIndexOf(u'.');
    return dot > 0 && dot < name.size() - 1
           && matcher.matches(QStringView(name).mid(dot + 1));
}

// 在 worker 线程枚举目录（纯函数：不触碰 DirectoryManager 成员，设置已快照）。
// 每 256 个条目检查一次代数，切目录时旧扫描立即放手，不与新扫描抢磁盘。
ScannedEntries scanDirectory(const QString &directoryPath, bool recursive,
                             const SuffixMatcher &matcher,
                             const std::atomic<quint64> *generation, quint64 myGeneration) {
    ScannedEntries out;
    std::error_code ec;
    const fs::path pathObj(directoryPath.toStdWString());

    int checked = 0;
    const auto outdated = [&]() {
        if (++checked < 256)
            return false;
        checked = 0;
        return generation->load(std::memory_order_relaxed) != myGeneration;
    };

    if (recursive) {
        fs::recursive_directory_iterator it(
            pathObj, fs::directory_options::skip_permission_denied, ec);
        if (ec)
            return out;

        for (const auto &entry : it) {
            if (outdated()) {
                out.cancelled = true;
                return out;
            }
            // generic_wstring(): Windows 上统一正斜杠分隔，与 Qt 路径约定一致
            QString path = QString::fromStdWString(entry.path().generic_wstring());
            QString name = FSEntry::extractFileName(path);

            if (!entry.is_directory(ec) && !ec && hasSupportedSuffix(name, matcher)) {
                if (auto newEntry = FSEntry::fromPath(path, name))
                    out.files.emplace_back(std::move(*newEntry));
            }
        }
        return out;
    }

    fs::directory_iterator it(pathObj, fs::directory_options::skip_permission_denied, ec);
    if (ec)
        return out;

    for (const auto &entry : it) {
        if (outdated()) {
            out.cancelled = true;
            return out;
        }
        QString path = QString::fromStdWString(entry.path().generic_wstring());
        QString name = FSEntry::extractFileName(path);

        if (entry.is_directory(ec) && !ec) {
            FSEntry newEntry;
            newEntry.name = std::move(name);
            newEntry.path = std::move(path);
            newEntry.isDirectory = true;
            out.dirs.emplace_back(std::move(newEntry));
        } else if (!ec && hasSupportedSuffix(name, matcher)) {
            // ⭐ QFileInfo 单次 stat 取齐 size/mtime
            if (auto newEntry = FSEntry::fromPath(path, name))
                out.files.emplace_back(std::move(*newEntry));
        }
    }
    return out;
}
} // namespace

namespace {
// setDirectory/setDirectoryRecursive/setDirectoryDirsOnly 共用前导检查：
// QFileInfo 首次查询即缓存 stat 元数据，存在性/类型/可读性共享同一次系统调用
//（原先 fs::status + QFileInfo 为两次 stat，且需 toStdWString 转换路径）
bool isReadableDir(const QFileInfo &dirInfo) {
    return dirInfo.exists() && dirInfo.isDir() && dirInfo.isReadable();
}

template<typename T, typename U, typename Pred>
typename std::vector<T>::iterator insert_sorted(std::vector<T> &vec, U &&item, Pred &&pred) {
    return vec.insert(std::upper_bound(vec.begin(), vec.end(), item, std::forward<Pred>(pred)), std::forward<U>(item));
}
} // namespace

bool DirectoryManager::path_entry_compare(const FSEntry &e1, const FSEntry &e2) const {
    return collator.compare(e1.path, e2.path) < 0;
}

bool DirectoryManager::path_entry_compare_reverse(const FSEntry &e1, const FSEntry &e2) const {
    return collator.compare(e1.path, e2.path) > 0;
}

bool DirectoryManager::name_entry_compare(const FSEntry &e1, const FSEntry &e2) const {
    return collator.compare(e1.name, e2.name) < 0;
}

bool DirectoryManager::name_entry_compare_reverse(const FSEntry &e1, const FSEntry &e2) const {
    return collator.compare(e1.name, e2.name) > 0;
}

bool DirectoryManager::date_entry_compare(const FSEntry& e1, const FSEntry& e2) const {
    return e1.modifyTime < e2.modifyTime;
}

bool DirectoryManager::date_entry_compare_reverse(const FSEntry& e1, const FSEntry& e2) const {
    return e1.modifyTime > e2.modifyTime;
}

bool DirectoryManager::size_entry_compare(const FSEntry& e1, const FSEntry& e2) const {
    return e1.size < e2.size;
}

bool DirectoryManager::size_entry_compare_reverse(const FSEntry& e1, const FSEntry& e2) const {
    return e1.size > e2.size;
}

// 修复编译错误：使用尾置返回类型
auto DirectoryManager::compareFunction() -> CompareFunction {
    switch (mSortingMode) {
        case SortingMode::SORT_NAME:
            return &DirectoryManager::name_entry_compare;
        case SortingMode::SORT_NAME_DESC:
            return &DirectoryManager::name_entry_compare_reverse;
        case SortingMode::SORT_TIME:
            return &DirectoryManager::date_entry_compare;
        case SortingMode::SORT_TIME_DESC:
            return &DirectoryManager::date_entry_compare_reverse;
        case SortingMode::SORT_SIZE:
            return &DirectoryManager::size_entry_compare;
        case SortingMode::SORT_SIZE_DESC:
            return &DirectoryManager::size_entry_compare_reverse;
        default:
            return &DirectoryManager::name_entry_compare;
    }
}

auto DirectoryManager::dirCompareFunction() -> CompareFunction {
    // sortFolders 关闭时整表排序走 byPath（路径升序），增量插入必须用同一顺序，
    // 否则 next/prevDirectory 看到的目录顺序与排序结果不一致
    return settings->sortFolders() ? compareFunction() : &DirectoryManager::path_entry_compare;
}

void DirectoryManager::startFileWatcher(const QString &directoryPath) {
    if(directoryPath.isEmpty()) return;
    const bool isFirstTime = !watcher;
    if(isFirstTime)
        watcher = DirectoryWatcher::newInstance();
    // ⭐ stopFileWatcher() 会断开连接（析构时停止监视）：
    // 重新打开目录时必须无条件恢复连接，否则此后自动刷新永久失效；
    // Qt::UniqueConnection 保证重复连接不会叠加
    connect(watcher, &DirectoryWatcher::fileCreated, this, &DirectoryManager::onFileAddedExternal, Qt::UniqueConnection);
    connect(watcher, &DirectoryWatcher::fileDeleted, this, &DirectoryManager::onFileRemovedExternal, Qt::UniqueConnection);
    connect(watcher, &DirectoryWatcher::fileModified, this, &DirectoryManager::onFileModifiedExternal, Qt::UniqueConnection);
    connect(watcher, &DirectoryWatcher::fileRenamed, this, &DirectoryManager::onFileRenamedExternal, Qt::UniqueConnection);
    connect(watcher, &DirectoryWatcher::watchPathReady, this, &DirectoryManager::onWatchPathReady, Qt::UniqueConnection);
    connect(watcher, &DirectoryWatcher::watchPathFailed, this, &DirectoryManager::onWatchPathFailed, Qt::UniqueConnection);
    connect(watcher, &DirectoryWatcher::watchPathOverflowed, this, &DirectoryManager::onWatchPathOverflowed, Qt::UniqueConnection);
    // 线程正在运行时，使用平滑路径切换（不重启线程）
    if(!isFirstTime && watcher->isObserving()) {
        watcher->requestWatchPath(directoryPath);
        return;
    }
    // 首次启动：设置路径并启动
    watcher->setWatchPath(directoryPath);
    watcher->observe();
}

void DirectoryManager::stopFileWatcher() {
    if(!watcher) return;
    disconnect(watcher, &DirectoryWatcher::fileCreated, this, &DirectoryManager::onFileAddedExternal);
    disconnect(watcher, &DirectoryWatcher::fileDeleted, this, &DirectoryManager::onFileRemovedExternal);
    disconnect(watcher, &DirectoryWatcher::fileModified, this, &DirectoryManager::onFileModifiedExternal);
    disconnect(watcher, &DirectoryWatcher::fileRenamed, this, &DirectoryManager::onFileRenamedExternal);
    disconnect(watcher, &DirectoryWatcher::watchPathReady, this, &DirectoryManager::onWatchPathReady);
    disconnect(watcher, &DirectoryWatcher::watchPathFailed, this, &DirectoryManager::onWatchPathFailed);
    disconnect(watcher, &DirectoryWatcher::watchPathOverflowed, this, &DirectoryManager::onWatchPathOverflowed);
    watcher->stopObserving();
    cancelPendingScan();
    mScanStartedWithoutWatch = false;
    clearPendingEvents();
}

void DirectoryManager::readSettings() {
    // 整体替换不可变快照：在飞扫描继续持有旧 matcher，无锁、无深拷贝
    mSuffixMatcher = std::make_shared<const SuffixMatcher>(settings->supportedFormats());
}

// ==================== 索引映射维护方法 ====================

void DirectoryManager::rebuildFileIndexMap() {
    mFileIndexMap.clear();
    mFileIndexMap.reserve(fileEntryVec.size());
    for(size_t i = 0; i < fileEntryVec.size(); ++i) {
        mFileIndexMap[fileEntryVec[i].path] = static_cast<int>(i);
    }
}

void DirectoryManager::rebuildDirIndexMap() {
    mDirIndexMap.clear();
    mDirIndexMap.reserve(dirEntryVec.size());
    for(size_t i = 0; i < dirEntryVec.size(); ++i) {
        mDirIndexMap[dirEntryVec[i].path] = static_cast<int>(i);
    }
}

void DirectoryManager::updateFileIndexAfterInsert(const QString &path, int index) {
    // 插入新元素
    mFileIndexMap[path] = index;

    // 更新 index 之后的所有元素（+1 已经体现在 vector 中，这里只需重写索引）
    for (int i = index + 1; i < static_cast<int>(fileEntryVec.size()); ++i) {
        mFileIndexMap[fileEntryVec[i].path] = i;
    }
}

void DirectoryManager::updateFileIndexAfterRemove(const QString &path, int index) {
    // 删除元素
    mFileIndexMap.erase(path);

    // 更新 index 之后的所有元素（vector 已前移）
    for (int i = index; i < static_cast<int>(fileEntryVec.size()); ++i) {
        mFileIndexMap[fileEntryVec[i].path] = i;
    }
}

void DirectoryManager::updateDirIndexAfterInsert(const QString &path, int index) {
    mDirIndexMap[path] = index;

    for (int i = index + 1; i < static_cast<int>(dirEntryVec.size()); ++i) {
        mDirIndexMap[dirEntryVec[i].path] = i;
    }
}

void DirectoryManager::updateDirIndexAfterRemove(const QString &path, int index) {
    mDirIndexMap.erase(path);

    for (int i = index; i < static_cast<int>(dirEntryVec.size()); ++i) {
        mDirIndexMap[dirEntryVec[i].path] = i;
    }
}

void DirectoryManager::refreshFileIndexRange(int first, int last) {
    first = qMax(0, first);
    last = qMin(last, static_cast<int>(fileEntryVec.size()) - 1);
    for (int i = first; i <= last; ++i)
        mFileIndexMap[fileEntryVec[i].path] = i;
}

void DirectoryManager::refreshDirIndexRange(int first, int last) {
    first = qMax(0, first);
    last = qMin(last, static_cast<int>(dirEntryVec.size()) - 1);
    for (int i = first; i <= last; ++i)
        mDirIndexMap[dirEntryVec[i].path] = i;
}

// ==================== 核心功能方法 ====================

bool DirectoryManager::setDirectory(const QString &dirPath) {
    // ⭐ 空路径 = 关闭目录（Core::reset）：取消扫描并立即清空列表，
    // 让 directoryPath() 立刻变空，避免沿用旧目录的索引
    if(dirPath.isEmpty()) {
        mScanGeneration.fetch_add(1, std::memory_order_relaxed);
        cancelPendingScan();
        mScanStartedWithoutWatch = false;
        mScanInProgress = false;
        mDirectoryPath.clear();
        mListSource = SOURCE_DIRECTORY;
        clearEntryList();
        // ⭐ 不停止 watcher：Core::reset() 之后会紧接着打开新目录，
        // 保持线程存活即可走 startFileWatcher() 的平滑路径切换；
        // 这期间到达的旧目录事件由 acceptsWatcherEvent() 按目录过滤丢弃
        return false;
    }
    // ⭐ QFileInfo 单次 stat 同时完成存在性、类型与可读性判断
    if(!isReadableDir(QFileInfo(dirPath))) {
        return false;
    }
    // 同一目录重载不是切目录：保留第一轮扫描期间缓冲的事件，交给最终结果统一重放。
    // 只有真正的切目录/关闭目录才允许清空事件队列。
    const bool sameDirectory = !mDirectoryPath.isEmpty()
                                && samePath(dirPath, mDirectoryPath);
    mListSource = SOURCE_DIRECTORY;
    mDirectoryPath = dirPath;
    loadEntryListAsync(dirPath, false, /*preservePendingEvents=*/sameDirectory);
    return true;
}

bool DirectoryManager::setDirectoryRecursive(const QString &dirPath) {
    // ⭐ QFileInfo 单次 stat 同时完成存在性、类型与可读性判断
    if(!isReadableDir(QFileInfo(dirPath))) {
        return false;
    }
    mListSource = SOURCE_DIRECTORY_RECURSIVE;
    mDirectoryPath = dirPath;
    loadEntryListAsync(dirPath, true);
    return true;
}

// 相邻目录切换用：只枚举子目录，文件元数据零 stat/零排序；不启动 watcher
// ⭐ 同一父目录（mtime 与排序设置均未变）时直接复用上次结果：
// next/prevDirectory 每次按键都会调用，原先每次都要全量重枚举 + 排序 + 重建索引
bool DirectoryManager::setDirectoryDirsOnly(const QString &dirPath) {
    // ⭐ QFileInfo 单次 stat 缓存元数据：可读性与父目录 mtime 共用
    const QFileInfo dirInfo(dirPath);
    if(!isReadableDir(dirInfo)) {
        return false;
    }

    const QDateTime mtime = dirInfo.lastModified();
    const SortingMode sortMode = settings->sortingMode();
    const bool sortFolders = settings->sortFolders();
    if(mListSource == SOURCE_DIRECTORY_DIRS_ONLY && mDirectoryPath == dirPath &&
       mDirsOnlyMtime == mtime &&
       mDirsOnlySortingMode == sortMode && mDirsOnlySortFolders == sortFolders) {
        return true;
    }

    mListSource = SOURCE_DIRECTORY_DIRS_ONLY;
    mDirectoryPath = dirPath;
    // 放弃可能在跑的目录扫描：其结果路径已不是当前目录
    mScanGeneration.fetch_add(1, std::memory_order_relaxed);
    cancelPendingScan();
    mScanStartedWithoutWatch = false;
    mScanInProgress = false;
    // 实例会长期存活（与一次性临时对象不同），排序模式每次都从设置同步
    mSortingMode = sortMode;
    mDirsOnlyMtime = mtime;
    mDirsOnlySortingMode = sortMode;
    mDirsOnlySortFolders = sortFolders;
    clearEntryList();
    mFilesSorted = true;
    addDirEntriesOnly(dirEntryVec, dirPath);
    sortDirEntryListsIncremental();
    rebuildDirIndexMap();
    return true;
}

// 性能优化：O(n) → O(1)
int DirectoryManager::indexOfFile(const QString &filePath) const {
    auto it = mFileIndexMap.find(filePath);
    return (it != mFileIndexMap.end()) ? it->second : -1;
}

// 性能优化：O(n) → O(1)
int DirectoryManager::indexOfDir(const QString &dirPath) const {
    auto it = mDirIndexMap.find(dirPath);
    return (it != mDirIndexMap.end()) ? it->second : -1;
}

const QString &DirectoryManager::filePathAt(int index) const {
    return checkFileRange(index) ? fileEntryVec.at(index).path : mEmptyString;
}

const QString &DirectoryManager::fileNameAt(int index) const {
    return checkFileRange(index) ? fileEntryVec.at(index).name : mEmptyString;
}

const QString &DirectoryManager::dirPathAt(int index) const {
    return checkDirRange(index) ? dirEntryVec.at(index).path : mEmptyString;
}

const QString &DirectoryManager::dirNameAt(int index) const {
    return checkDirRange(index) ? dirEntryVec.at(index).name : mEmptyString;
}

const QString &DirectoryManager::firstFile() const {
    return fileEntryVec.empty() ? mEmptyString : fileEntryVec.front().path;
}

const QString &DirectoryManager::lastFile() const {
    return fileEntryVec.empty() ? mEmptyString : fileEntryVec.back().path;
}

// 性能优化：indexOfFile 现在是 O(1)
const QString &DirectoryManager::prevOfFile(const QString &filePath) const {
    int currentIndex = indexOfFile(filePath);
    return (currentIndex > 0) ? fileEntryVec.at(currentIndex - 1).path : mEmptyString;
}

const QString &DirectoryManager::nextOfFile(const QString &filePath) const {
    int currentIndex = indexOfFile(filePath);
    return (currentIndex >= 0 && currentIndex < static_cast<int>(fileEntryVec.size()) - 1) ? fileEntryVec.at(currentIndex + 1).path : mEmptyString;
}

const QString &DirectoryManager::prevOfDir(const QString &dirPath) const {
    int currentIndex = indexOfDir(dirPath);
    return (currentIndex > 0) ? dirEntryVec.at(currentIndex - 1).path : mEmptyString;
}

const QString &DirectoryManager::nextOfDir(const QString &dirPath) const {
    int currentIndex = indexOfDir(dirPath);
    return (currentIndex >= 0 && currentIndex < static_cast<int>(dirEntryVec.size()) - 1) ? dirEntryVec.at(currentIndex + 1).path : mEmptyString;
}

const FSEntry &DirectoryManager::fileEntryAt(int index) const {
    if(checkFileRange(index))
        return fileEntryVec.at(index);
    return defaultEntry;
}

bool DirectoryManager::isFile(const QString &path) const {
    std::filesystem::path pathObj(path.toStdWString());
    std::error_code ec;
    auto status = std::filesystem::status(pathObj, ec);
    if (ec) return false;
    return std::filesystem::is_regular_file(status);
}

bool DirectoryManager::isDir(const QString &path) const {
    std::filesystem::path pathObj(path.toStdWString());
    std::error_code ec;
    auto status = std::filesystem::status(pathObj, ec);
    if (ec) return false;
    return std::filesystem::is_directory(status);
}

void DirectoryManager::clearEntryListData() {
    fileEntryVec.clear();
    dirEntryVec.clear();
    mFileIndexMap.clear();
    mDirIndexMap.clear();
    // 重置增量排序标志，确保下一批结果（或增量插入）重新排序
    mFilesSorted = false;
    mDirsSorted = false;
    mLastCompareFunction = nullptr;
}

void DirectoryManager::clearEntryList() {
    clearEntryListData();
    // 切目录/关闭目录才丢弃积压事件；同目录排序重扫必须保留，
    // 否则排序快照过期时第一轮扫描期间的事件会被第二轮扫描"间接补偿"
    clearPendingEvents();
}

bool DirectoryManager::startPendingScan() {
    if (!mScanStartPending)
        return false;

    if (mPendingScanGeneration != mScanGeneration.load(std::memory_order_relaxed)
        || !samePath(mPendingScanPath, mDirectoryPath)) {
        cancelPendingScan();
        return false;
    }

    mScanStartPending = false;
    mWatchReadyTimer.stop();
    dispatchScan(mPendingScanPath, mPendingScanRecursive, mPendingScanGeneration);
    return true;
}

void DirectoryManager::onWatchReadyTimeout() {
    if (!mScanStartPending)
        return;
    // ready 信号可能还在事件队列里：先查权威状态，避免把已 arm 的扫描误判成兜底
    if (watcher && watcher->isWatchPathReady(mPendingScanPath)) {
        mScanStartedWithoutWatch = false;
        startPendingScan();
        return;
    }
    // watcher 迟迟未 arm：先保证目录能显示；若之后 arm，onWatchPathReady() 会补扫一次，
    // 把「扫描开始 → 真正 arm」这段窗口内的变化重新核对掉。
    if (startPendingScan())
        mScanStartedWithoutWatch = true;
}

void DirectoryManager::cancelPendingScan() {
    mScanStartPending = false;
    mWatchReadyTimer.stop();
}

// ⭐ 枚举 + FSEntry 构建 + 排序全部移到 worker：
// 10k 文件的目录原先在 GUI 线程同步枚举+stat+排序，会阻塞上百毫秒到数秒。
// GUI 线程只做「接收结果 + 建立两个索引映射」。
void DirectoryManager::loadEntryListAsync(const QString &directoryPath, bool recursive,
                                          bool preservePendingEvents) {
    cancelPendingScan();
    if (preservePendingEvents)
        clearEntryListData();
    else {
        clearEntryList();
        // 新目录：上一次超时兜底标记不再适用
        mScanStartedWithoutWatch = false;
    }
    mScanInProgress = true;

    const quint64 generation = mScanGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
    mPendingScanPath = directoryPath;
    mPendingScanGeneration = generation;
    mPendingScanRecursive = recursive;
    mScanStartPending = true;

    // ⭐ 扫描必须等 watcher 真正 arm：请求切换路径只是登记，worker 关闭旧句柄、
    // 打开新句柄并发出第一次 ReadDirectoryChangesW 之间存在交接窗口。
    // 在该窗口内开始扫描，扫描已越过的路径若随后变化且 watcher 尚未 arm，就永久漏事件。
    // arm 完成后才扫描：arm 之前的变化由扫描直接看到，arm 之后的由事件缓冲重放。
    // 递归模式不启动/停止 watcher（事件由 acceptsWatcherEvent 按 SOURCE_DIRECTORY 过滤），
    // 避免 stop/observe 与线程退出竞争，导致下一次普通目录切换时 watcher 实际未运行。
    if(mListSource == SOURCE_DIRECTORY) {
        startFileWatcher(directoryPath);
        if (mScanStartPending) {
            // 已就绪（同目录重扫或 dummy 平台）立即扫描；否则等信号/失败/超时
            if (watcher && watcher->isWatchPathReady(directoryPath)) {
                // 扫描在 arm 之后开始，超时兜底标记不再适用
                mScanStartedWithoutWatch = false;
                startPendingScan();
            } else {
                mWatchReadyTimer.start();
            }
        }
        return;
    }

    mScanStartedWithoutWatch = false;
    startPendingScan();
}

void DirectoryManager::dispatchScan(const QString &directoryPath, bool recursive,
                                    quint64 generation) {
    // 值快照：worker 不读 Settings，也不碰其余成员。
    // shared_ptr 快照仅递增引用计数（noexcept、零分配）：
    // 替代原先深拷贝全部分桶（每次扫描 ~17 次堆分配，且是 runnable 内唯一可能抛异常的捕获）
    const std::shared_ptr<const SuffixMatcher> matcher = mSuffixMatcher;
    const SortingMode sortMode = mSortingMode;
    const bool sortFolders = settings->sortFolders();

    DirectoryManager *self = this;
    mScanPool.start(QRunnable::create(
        [self, directoryPath, recursive, matcher, sortMode, sortFolders, generation] {
            // 异常逃出 runnable 会直接 std::terminate：扫描/排序全程兜底。
            // 失败时按空结果收敛（仍走 applyScanResult：代数校验 + 重放缓冲事件 +
            // emit loaded），避免 UI 永久停留在 isScanning()
            try {
                ScannedEntries entries =
                    scanDirectory(directoryPath, recursive, *matcher,
                                  &self->mScanGeneration, generation);
                if (entries.cancelled)
                    return;

                QCollator workerCollator = makeCollator();
                sortEntries(entries.files, sortMode, false, workerCollator);
                sortEntries(entries.dirs, sortMode, !sortFolders, workerCollator);

                QMetaObject::invokeMethod(self,
                    [self, entries = std::move(entries), generation, directoryPath, sortMode, sortFolders]() mutable {
                        self->applyScanResult(std::move(entries.files), std::move(entries.dirs),
                                              generation, directoryPath, sortMode, sortFolders);
                    },
                    Qt::QueuedConnection);
            } catch (...) {
                qWarning() << "directory scan failed, falling back to empty result:" << directoryPath;
                QMetaObject::invokeMethod(self,
                    [self, generation, directoryPath, sortMode, sortFolders] {
                        self->applyScanResult({}, {}, generation, directoryPath,
                                              sortMode, sortFolders);
                    },
                    Qt::QueuedConnection);
            }
        }));
}

void DirectoryManager::applyScanResult(std::vector<FSEntry> files, std::vector<FSEntry> dirs,
                                       quint64 generation, const QString &path,
                                       SortingMode scannedSortMode, bool scannedSortFolders) {
    // 迟到的结果：期间已切目录 / 结构变化过，直接丢弃
    if(generation != mScanGeneration.load(std::memory_order_relaxed) || path != mDirectoryPath)
        return;

    // 扫描期间排序设置可能被改过（列表为空时 setSortingMode 不会重排），
    // 此时 worker 的结果顺序已作废：直接按当前设置重扫（仍在线程池），
    // 不在 GUI 线程对 10 万条 FSEntry 做整表排序
    if(scannedSortMode != mSortingMode || scannedSortFolders != settings->sortFolders()) {
        // 同目录仅排序快照过期：保留第一轮扫描期间缓冲的事件，交给最终结果统一重放。
        // 只有切目录/关闭目录才允许清空事件队列。
        loadEntryListAsync(path, mListSource == SOURCE_DIRECTORY_RECURSIVE,
                           /*preservePendingEvents=*/true);
        return;
    }

    mScanInProgress = false;
    fileEntryVec = std::move(files);
    dirEntryVec = std::move(dirs);

    rebuildFileIndexMap();
    rebuildDirIndexMap();

    // worker 已完成排序：同步排序状态，避免随后的增量插入/重排再做整表排序
    mFilesSorted = true;
    mDirsSorted = true;
    mLastCompareFunction = compareFunction();

    // ⭐ 重放扫描期间缓冲的 watcher 事件。事件处理本身会 stat 校验磁盘状态，
    // 因此「扫描已包含的变化」会自然收敛为幂等操作。
    // 必须在 loaded 之前完成：否则 Core 可能先按过期列表打开首图/末图。
    flushPendingEvents();

    emit loaded(path);
}

void DirectoryManager::addDirEntriesOnly(std::vector<FSEntry> &entryVec, const QString &directoryPath) {
    std::filesystem::path pathObj(directoryPath.toStdWString());
    std::error_code ec;
    fs::directory_iterator it(pathObj, fs::directory_options::skip_permission_denied, ec);
    if (ec) return;

    for (const auto& entry : it) {
        if (!entry.is_directory(ec) || ec)
            continue;
        // generic_wstring(): Windows 上统一正斜杠分隔，与 Qt 全局路径约定一致
        QString path = QString::fromStdWString(entry.path().generic_wstring());
        FSEntry newEntry;
        newEntry.name = FSEntry::extractFileName(path);
        newEntry.path = std::move(path);
        newEntry.isDirectory = true;
        entryVec.emplace_back(std::move(newEntry));
    }
}

void DirectoryManager::sortEntryLists() {
    sortFileEntryListsIncremental();
    sortDirEntryListsIncremental();
    // 排序后索引变化，需要重建映射
    rebuildFileIndexMap();
    rebuildDirIndexMap();
}

void DirectoryManager::sortFileEntryListsIncremental() {
    CompareFunction currentCompareFn = compareFunction();
    if (mLastCompareFunction == currentCompareFn && mFilesSorted && fileEntryVec.size() > 1) {
        return;
    }
    sortEntries(fileEntryVec, mSortingMode, false, collator);
    mLastCompareFunction = currentCompareFn;
    mFilesSorted = true;
}

void DirectoryManager::sortDirEntryListsIncremental() {
    CompareFunction currentCompareFn = compareFunction();
    if (mLastCompareFunction == currentCompareFn && mDirsSorted && dirEntryVec.size() > 1) {
        return;
    }
    // sortFolders 关闭时目录始终按路径升序（与旧实现一致）
    sortEntries(dirEntryVec, mSortingMode, !settings->sortFolders(), collator);
    mLastCompareFunction = currentCompareFn;
    mDirsSorted = true;
}

void DirectoryManager::setSortingMode(SortingMode mode) {
    if(mode != mSortingMode) {
        mSortingMode = mode;
        if(fileEntryVec.size() > 1 || dirEntryVec.size() > 1) {
            sortEntryLists();
            emit sortingChanged();
        }
    }
}

bool DirectoryManager::insertFileEntry(const QString &filePath) {
    // ⭐ QFileInfo 单次 stat 取齐类型与元数据，替代 directory_entry 构造 + 后续重复 stat
    auto entry = FSEntry::fromPath(filePath);
    if(!entry || entry->isDirectory)
        return false;

    // 用 basename 取后缀（与目录扫描一致），避免目录名中的点被误判为文件后缀
    if(!hasSupportedSuffix(entry->name, *mSuffixMatcher))
        return false;

    return forceInsertFileEntry(filePath, *entry);
}

bool DirectoryManager::forceInsertFileEntry(const QString &filePath) {
    auto entry = FSEntry::fromPath(filePath);
    if(!entry || entry->isDirectory)
        return false;
    return forceInsertFileEntry(filePath, *entry);
}

bool DirectoryManager::forceInsertFileEntry(const QString &filePath, const FSEntry &entry) {
    if(containsFile(filePath))
        return false;
    // FSEntry 已含全部元数据（单次 stat），直接插入，无额外 stat
    auto cmpFn = compareFunction();
    auto it = insert_sorted(fileEntryVec, entry, [this, cmpFn](const FSEntry& a, const FSEntry& b) {
        return (this->*cmpFn)(a, b);
    });

    int index = static_cast<int>(it - fileEntryVec.begin());
    // 同步更新索引映射
    updateFileIndexAfterInsert(filePath, index);
    emit fileAdded(filePath);
    return true;
}

void DirectoryManager::removeFileEntry(const QString &filePath) {
    if(!containsFile(filePath))
        return;
    int index = indexOfFile(filePath);
    fileEntryVec.erase(fileEntryVec.begin() + index);
    // 同步更新索引映射
    updateFileIndexAfterRemove(filePath, index);
    emit fileRemoved(filePath, index);
}

void DirectoryManager::updateFileEntry(const QString &filePath) {
    if(!containsFile(filePath))
        return;
    QFileInfo fi(filePath);
    updateFileEntry(filePath, fi);
}

void DirectoryManager::updateFileEntry(const QString &filePath, const QFileInfo &fi) {
    // ⭐ 调用方已持有 QFileInfo（exists/stat 已完成），此处复用，不再二次 stat
    if(!containsFile(filePath) || !fi.exists())
        return;
    int index = indexOfFile(filePath);
    if(fileEntryVec.at(index).refresh(fi)) {
        emit fileModified(filePath);
    }
}

bool DirectoryManager::renameFileEntryBatch(const QString &oldPath, const QString &newName,
                                             FileRenameEmit &emitOut, QVector<QPair<QString,int>> &extraRemoves,
                                             QVector<QString> &extraAdds, QVector<QString> &extraRefresh) {
    QFileInfo fi(oldPath);
    const QString newFilePath = fi.absolutePath() + "/" + newName;
    if(!containsFile(oldPath)) {
        // fallback 交由上层统一处理（避免批量中穿插 insert/remove 打乱索引）
        if(containsFile(newFilePath))
            extraRefresh.append(newFilePath);
        else
            extraAdds.append(newFilePath);
        return false;
    }
    auto newEntryOpt = FSEntry::fromPath(newFilePath, newName);
    if(!newEntryOpt || newEntryOpt->isDirectory) {
        const int idx = indexOfFile(oldPath);
        extraRemoves.append(QPair<QString,int>(oldPath, idx));
        fileEntryVec.erase(fileEntryVec.begin() + idx);
        // 内核自行维护索引不变量：任何 erase 都必须同步映射（调用方无需兜底 rebuild）
        updateFileIndexAfterRemove(oldPath, idx);
        return false;
    }
    if(!hasSupportedSuffix(newEntryOpt->name, *mSuffixMatcher)) {
        const int idx = indexOfFile(oldPath);
        extraRemoves.append(QPair<QString,int>(oldPath, idx));
        fileEntryVec.erase(fileEntryVec.begin() + idx);
        updateFileIndexAfterRemove(oldPath, idx);
        return false;
    }
    int oldIndex = indexOfFile(oldPath);
    const int emitOldIndex = oldIndex;
    const int replaceIndex = containsFile(newFilePath) ? indexOfFile(newFilePath) : -1;
    if(replaceIndex != -1) {
        if(replaceIndex < oldIndex) {
            fileEntryVec.erase(fileEntryVec.begin() + replaceIndex);
            --oldIndex;
        } else if(replaceIndex > oldIndex) {
            fileEntryVec.erase(fileEntryVec.begin() + replaceIndex);
        }
    }
    fileEntryVec.erase(fileEntryVec.begin() + oldIndex);
    auto cmpFn = compareFunction();
    auto it = insert_sorted(fileEntryVec, std::move(*newEntryOpt), [this, cmpFn](const FSEntry& a, const FSEntry& b) {
        return (this->*cmpFn)(a, b);
    });
    const int newIndex = static_cast<int>(it - fileEntryVec.begin());

    // 删除 + 插入对索引的影响只发生在三个位置之间：只刷新该区间，
    // 不再为每条重命名做一次 O(n) 的整表 rebuild（vector 搬移本身仍由插入位置决定）
    mFileIndexMap.erase(oldPath);
    mFileIndexMap.erase(newFilePath);
    mFileIndexMap[newFilePath] = newIndex;
    int first = qMin(oldIndex, newIndex);
    int last = qMax(oldIndex, newIndex);
    if(replaceIndex != -1) {
        // 覆盖重命名是"两次删除 + 一次插入"（净 -1），尾段索引整体前移
        first = qMin(first, replaceIndex);
        last = static_cast<int>(fileEntryVec.size()) - 1;
    }
    refreshFileIndexRange(first, last);

    emitOut = {oldPath, emitOldIndex, newFilePath, newIndex};
    return true;
}

bool DirectoryManager::renameDirEntryBatch(const QString &oldPath, const QString &newName) {
    if(!containsDir(oldPath))
        return false;
    QFileInfo fi(oldPath);
    const QString newDirPath = fi.absolutePath() + "/" + newName;
    int oldIndex = indexOfDir(oldPath);
    const int replaceIndex = containsDir(newDirPath) ? indexOfDir(newDirPath) : -1;
    if(replaceIndex != -1) {
        if(replaceIndex < oldIndex) {
            dirEntryVec.erase(dirEntryVec.begin() + replaceIndex);
            --oldIndex;
        } else if(replaceIndex > oldIndex) {
            dirEntryVec.erase(dirEntryVec.begin() + replaceIndex);
        }
    }
    dirEntryVec.erase(dirEntryVec.begin() + oldIndex);
    FSEntry newEntry;
    newEntry.name = newName;
    newEntry.path = newDirPath;
    newEntry.isDirectory = true;
    auto cmpFn = dirCompareFunction();
    auto it = insert_sorted(dirEntryVec, std::move(newEntry), [this, cmpFn](const FSEntry& a, const FSEntry& b) {
        return (this->*cmpFn)(a, b);
    });
    const int newIndex = static_cast<int>(it - dirEntryVec.begin());

    mDirIndexMap.erase(oldPath);
    mDirIndexMap.erase(newDirPath);
    mDirIndexMap[newDirPath] = newIndex;
    int first = qMin(oldIndex, newIndex);
    int last = qMax(oldIndex, newIndex);
    if(replaceIndex != -1) {
        // 覆盖重命名是"两次删除 + 一次插入"（净 -1），尾段索引整体前移
        first = qMin(first, replaceIndex);
        last = static_cast<int>(dirEntryVec.size()) - 1;
    }
    refreshDirIndexRange(first, last);
    return true;
}

void DirectoryManager::renameFileEntry(const FilePath& oldFilePath, const FileName& newFileName) {
    // ⭐ 单条重命名走批量内核，保证与 processPendingRenames 同一语义（索引区间更新也在内核里）
    FileRenameEmit emitInfo;
    QVector<QPair<QString,int>> extraRemoves;
    QVector<QString> extraAdds;
    QVector<QString> extraRefresh;
    const bool ok = renameFileEntryBatch(oldFilePath.value, newFileName.value,
                                         emitInfo, extraRemoves, extraAdds, extraRefresh);
    if(!ok) {
        // fallback：内核已在所有 erase 分支同步维护索引，这里只处理额外新增/刷新
        for(const auto &p : extraRefresh)
            updateFileEntry(p);
        for(const auto &p : extraAdds)
            insertFileEntry(p);
        for(const auto &r : extraRemoves)
            emit fileRemoved(r.first, r.second);
        return;
    }
    // 索引映射已在 renameFileEntryBatch 内按影响区间更新，无需整表 rebuild
    emit fileRenamed(emitInfo.from, emitInfo.fromIndex, emitInfo.to, emitInfo.toIndex);
}

bool DirectoryManager::insertDirEntry(const QString &dirPath) {
    if(containsDir(dirPath))
        return false;
    // 不再构造 directory_entry 做无谓 stat，文件名直接由路径解析
    QString dirName = FSEntry::extractFileName(dirPath);
    FSEntry newEntry;
    newEntry.name = std::move(dirName);
    newEntry.path = dirPath;
    newEntry.isDirectory = true;

    // 优化：使用 lambda 替代 std::bind
    auto cmpFn = dirCompareFunction();
    auto it = insert_sorted(dirEntryVec, std::move(newEntry), [this, cmpFn](const FSEntry& a, const FSEntry& b) {
        return (this->*cmpFn)(a, b);
    });

    int index = static_cast<int>(it - dirEntryVec.begin());
    // 同步更新索引映射
    updateDirIndexAfterInsert(dirPath, index);
    return true;
}

void DirectoryManager::removeDirEntry(const QString &dirPath) {
    if(!containsDir(dirPath))
        return;
    int index = indexOfDir(dirPath);
    dirEntryVec.erase(dirEntryVec.begin() + index);
    // 同步更新索引映射
    updateDirIndexAfterRemove(dirPath, index);
}

void DirectoryManager::renameDirEntry(const DirPath& oldDirPath, const DirName& newDirName) {
    // 批量内核已同步更新索引映射
    renameDirEntryBatch(oldDirPath.value, newDirName.value);
}

QStringList DirectoryManager::fileList() const {
    QStringList list;
    list.reserve(static_cast<int>(fileEntryVec.size()));
    for(auto const& value : fileEntryVec)
        list << value.path;
    return list;
}

bool DirectoryManager::acceptsWatcherEvent(const QString &directoryPath) const {
    // 目录已关闭（Core::reset 与下一次 setDirectory 之间的空窗）、
    // 已切到递归/目录列表模式，或事件来自刚切走的旧目录，一律丢弃
    return !mIgnoreWatcherEvents
           && mListSource == SOURCE_DIRECTORY
           && !mDirectoryPath.isEmpty()
           && samePath(directoryPath, mDirectoryPath);
}

void DirectoryManager::queueWatcherEvent(PendingEvent ev) {
    mPendingEvents.append(std::move(ev));
    // 扫描期间只入队，不启动定时器：列表尚在构建，applyScanResult() 会统一重放
    if(!mScanInProgress)
        mEventBatchTimer.start();
}

void DirectoryManager::onWatchPathReady(const QString &directoryPath) {
    if(mScanStartPending && samePath(directoryPath, mPendingScanPath)) {
        // 信号可能已在队列里停留过久：以 worker 的权威状态为准，
        // 避免路径 A→B→A 后旧的 ready 信号让扫描抢在新 arm 之前启动。
        if(watcher && watcher->isWatchPathReady(mPendingScanPath)) {
            mScanStartedWithoutWatch = false;
            startPendingScan();
        }
        return;
    }

    // 超时兜底扫描已经开始，watcher 现在才 arm：重新枚举一次，
    // 关闭「旧扫描越过某路径 → 新 watcher 尚未 arm」的竞态窗口。
    // 这里不能要求 isWatchPathReady() 为 true：信号可能在队列里停留期间
    // 又完成了一次读取。loadEntryListAsync() 会重新等待下一次 arm 再扫描。
    if(!mScanStartPending
       && mScanStartedWithoutWatch
       && mListSource == SOURCE_DIRECTORY
       && watcher
       && samePath(directoryPath, mDirectoryPath)) {
        mScanStartedWithoutWatch = false;
        loadEntryListAsync(mDirectoryPath, false, /*preservePendingEvents=*/true);
    }
}

void DirectoryManager::onWatchPathFailed(const QString &directoryPath) {
    // watcher 无法建立时不能把目录加载一起卡住：立即退化为普通扫描。
    // 若这是旧 worker 的失败、之后新 worker 又成功 arm，按兜底扫描做补偿。
    if(!mScanStartPending || !samePath(directoryPath, mPendingScanPath))
        return;

    // 失败信号也可能滞后：若当前句柄其实已经 arm，直接按正常就绪处理
    if(watcher && watcher->isWatchPathReady(mPendingScanPath)) {
        mScanStartedWithoutWatch = false;
        startPendingScan();
        return;
    }
    if(startPendingScan())
        mScanStartedWithoutWatch = true;
}

void DirectoryManager::onWatchPathOverflowed(const QString &directoryPath) {
    if(!acceptsWatcherEvent(directoryPath))
        return;

    // Windows 通知缓冲溢出意味着部分变化已经丢失，只能以磁盘状态为准重扫。
    // 保留已收到的事件；loadEntryListAsync() 会重新等待 watcher arm 后再扫描，
    // 避免重扫本身又落在“旧读完成、新读尚未挂起”的窗口里。
    loadEntryListAsync(mDirectoryPath, /*recursive=*/false,
                       /*preservePendingEvents=*/true);
}

void DirectoryManager::onFileRemovedExternal(const QString &directoryPath, const QString &fileName) {
    if(!acceptsWatcherEvent(directoryPath))
        return;

    PendingEvent ev;
    ev.type = PendingEvent::Type::Remove;
    ev.path = QDir(directoryPath).filePath(fileName);
    queueWatcherEvent(std::move(ev));
}

void DirectoryManager::onFileAddedExternal(const QString &directoryPath, const QString &fileName) {
    if(!acceptsWatcherEvent(directoryPath))
        return;

    PendingEvent ev;
    ev.type = PendingEvent::Type::Add;
    ev.path = QDir(directoryPath).filePath(fileName);
    queueWatcherEvent(std::move(ev));
}

void DirectoryManager::onFileRenamedExternal(const QString &directoryPath,
                                             const QString &oldName, const QString &newName) {
    if(!acceptsWatcherEvent(directoryPath))
        return;

    PendingEvent ev;
    ev.type = PendingEvent::Type::Rename;
    ev.path = QDir(directoryPath).filePath(oldName);
    ev.newName = newName;
    queueWatcherEvent(std::move(ev));
}

void DirectoryManager::onFileModifiedExternal(const QString &directoryPath, const QString &fileName) {
    if(!acceptsWatcherEvent(directoryPath))
        return;

    PendingEvent ev;
    ev.type = PendingEvent::Type::Modify;
    ev.path = QDir(directoryPath).filePath(fileName);
    queueWatcherEvent(std::move(ev));
}

// ==================== 批量事件处理 ====================

void DirectoryManager::clearPendingEvents() {
    mEventBatchTimer.stop();
    mPendingEvents.clear();
}

void DirectoryManager::flushPendingEvents() {
    if(mIgnoreWatcherEvents) {
        clearPendingEvents();
        return;
    }
    // 扫描未完成时列表还是空的/旧的：事件留到 applyScanResult() 统一重放
    if(mScanInProgress)
        return;

    // 快照并立即清空，处理期间新到达的事件进入下一批
    QVector<PendingEvent> events = std::move(mPendingEvents);
    mPendingEvents.clear();

    // ---- 按真实到达顺序合并 ----
    // add/remove：同一路径由最后一次到达的 op 决定（跨队列时序不再丢失）
    QHash<QString, bool> finalOp;          // true=add, false=remove
    // rename：同旧路径只保留最后一次；renameSeq 记录其序号用于按序输出
    QHash<QString, QString> renameFinal;
    QHash<QString, int> renameSeq;
    QHash<QString, int> firstRenameSeq;    // 旧路径首次被 rename 的序号
    QHash<QString, int> addSeq;            // add 事件序号（判定"新建后随即重命名"）
    QSet<QString> modifySet;
    int seq = 0;
    for(const auto &ev : events) {
        switch(ev.type) {
        case PendingEvent::Type::Add:
            addSeq.insert(ev.path, seq);
            finalOp.insert(ev.path, true);
            break;
        case PendingEvent::Type::Remove:
            finalOp.insert(ev.path, false);
            break;
        case PendingEvent::Type::Rename:
            if(!firstRenameSeq.contains(ev.path))
                firstRenameSeq.insert(ev.path, seq);
            renameFinal.insert(ev.path, ev.newName);
            renameSeq.insert(ev.path, seq);
            break;
        case PendingEvent::Type::Modify:
            modifySet.insert(ev.path);
            break;
        }
        ++seq;
    }

    // rename 按到达顺序输出（同旧路径取最后一次），保证链式 rename 可顺序套用
    QVector<QPair<QString, QString>> renames;
    {
        QVector<QPair<QString, int>> ordered;
        ordered.reserve(renameSeq.size());
        for(auto it = renameSeq.constBegin(); it != renameSeq.constEnd(); ++it)
            ordered.append(QPair<QString, int>(it.key(), it.value()));
        std::sort(ordered.begin(), ordered.end(),
                  [](const QPair<QString, int> &a, const QPair<QString, int> &b) {
                      return a.second < b.second;
                  });
        renames.reserve(ordered.size());
        for(const auto &o : ordered)
            renames.append(QPair<QString, QString>(o.first, renameFinal.value(o.first)));
    }

    // "新建后随即重命名"：add 早于该路径首次 rename → 该 add 被消费，不再单独入列表
    QSet<QString> consumedAdds;
    for(auto it = addSeq.constBegin(); it != addSeq.constEnd(); ++it) {
        auto f = firstRenameSeq.constFind(it.key());
        if(f != firstRenameSeq.constEnd() && it.value() < f.value())
            consumedAdds.insert(it.key());
    }

    QVector<QString> removes;
    QVector<QString> adds;
    for(auto it = finalOp.constBegin(); it != finalOp.constEnd(); ++it) {
        if(it.value()) {
            if(!consumedAdds.contains(it.key()))
                adds.append(it.key());
        } else {
            removes.append(it.key());
        }
    }

    // 处理顺序：删除 → 重命名 → 新增 → 修改
    // （先删后改名："删除 b + 把 a 改名为 b"必须按磁盘时序先删 b，rename 覆盖语义才正确）
    processPendingRemovals(removes);
    processPendingRenames(renames);
    processPendingAdditions(adds);
    processPendingModifications(modifySet);
}

void DirectoryManager::processPendingRenames(const QVector<QPair<QString, QString>> &renames) {
    if(renames.isEmpty())
        return;

    // ⭐ 逐条即时更新（文件重命名还需 emit），保证索引与视图增量一致（链式 rename 依赖新鲜索引）；
    // 批量内核只刷新受影响区间，不再每条重命名都整表 rebuild
    for(const auto &r : renames) {
        const QString &oldPath = r.first;
        const QString &newName = r.second;

        // ⭐ 优先用已有索引判断（避免文件系统时序问题）
        if(containsDir(oldPath)) {
            renameDirEntryBatch(oldPath, newName);
            continue;
        }
        if(containsFile(oldPath)) {
            FileRenameEmit emitInfo;
            QVector<QPair<QString,int>> extraRemoves;
            QVector<QString> extraAdds;
            QVector<QString> extraRefresh;
            if(renameFileEntryBatch(oldPath, newName, emitInfo,
                                    extraRemoves, extraAdds, extraRefresh)) {
                emit fileRenamed(emitInfo.from, emitInfo.fromIndex, emitInfo.to, emitInfo.toIndex);
            } else {
                // 内核已维护索引不变量：只应用额外新增/刷新，不再整表 rebuild
                for(const auto &er : extraRemoves)
                    emit fileRemoved(er.first, er.second);
                for(const auto &p : extraRefresh)
                    updateFileEntry(p);
                for(const auto &p : extraAdds)
                    insertFileEntry(p);
            }
            continue;
        }

        // ⭐ fallback 单次 QFileInfo 复用，避免 isDir+isFile 两次 stat。
        // 新路径由事件自身的旧路径推导，不依赖 watcher 当前路径（扫描重放时更稳）
        const QString newPath = QDir(QFileInfo(oldPath).absolutePath()).filePath(newName);
        const QFileInfo fi(newPath);
        if(!fi.exists())
            continue;
        if(fi.isDir()) {
            insertDirEntry(newPath);
        } else if(fi.isFile()) {
            if(containsFile(newPath))
                updateFileEntry(newPath, fi);
            else
                insertFileEntry(newPath);
        }
    }
}

namespace {
// 新增：增量插入省掉一次 O(n log n) 整表排序（n=100k 时 log n ≈ 17），
// 小批量 k < log n 时 O(k·n) 仍更划算；解压/批量拷贝走整表排序
constexpr size_t kIncrementalAddLimit = 8;
// 删除：批量路径本身就是 O(n)（survivors 复制 + 单次索引重建），
// 多条走增量会变成 k×O(n) 的搬移 + 索引平移，只有单条删除才更划算
constexpr size_t kIncrementalRemoveLimit = 1;
} // namespace

void DirectoryManager::processPendingRemovals(const QVector<QString> &removes) {
    if(removes.isEmpty())
        return;

    // 去重
    QVector<QString> paths;
    {
        QSet<QString> seen;
        paths.reserve(removes.size());
        for(const auto &p : removes) {
            if(!seen.contains(p)) {
                seen.insert(p);
                paths.append(p);
            }
        }
    }

    // 目录删除
    {
        QVector<QString> toRemove;
        for(const auto &p : paths)
            if(containsDir(p))
                toRemove.append(p);
        if(!toRemove.isEmpty()) {
            if(static_cast<size_t>(toRemove.size()) <= kIncrementalRemoveLimit) {
                // 单条路径：erase + 索引前移（O(n)，无整表重建）
                for(const auto &p : toRemove)
                    removeDirEntry(p);
            } else {
                // 批量：一次性擦除 → 单次重建索引
                QSet<QString> removalSet;
                for(const auto &p : toRemove) removalSet.insert(p);
                std::vector<FSEntry> survivors;
                survivors.reserve(dirEntryVec.size() - static_cast<size_t>(toRemove.size()));
                for(auto &e : dirEntryVec)
                    if(!removalSet.contains(e.path))
                        survivors.push_back(std::move(e));
                dirEntryVec = std::move(survivors);
                rebuildDirIndexMap();
            }
        }
    }

    // 文件删除：同理（此时 dirCount 已更新，视图索引按当前目录数计算）
    {
        // ⭐ 原子替换（如 QSaveFile 提交：删除+重建）同样会触发 REMOVED 事件，
        // 但文件仍在磁盘上 → 实为"替换"而非删除：刷新元数据即可，不删除条目，
        // 避免 Core::onFileRemoved 误判为真删除而跳到下一张图片
        // ⭐ 单个 QFileInfo 复用：exists 判断与 updateFileEntry 共用一次 stat
        struct KeptEntry { QString path; QFileInfo fi; };
        QVector<KeptEntry> kept;
        QVector<QPair<QString, int>> toRemove;
        for(const auto &p : paths) {
            if(!containsFile(p))
                continue;
            QFileInfo fi(p);
            if(fi.exists()) {
                // QFileInfo 无移动构造，std::move 只会回退为拷贝，直接传即可
                kept.append({p, fi});
                continue;
            }
            toRemove.append(QPair<QString, int>(p, indexOfFile(p)));
        }
        if(!kept.isEmpty())
            for(const auto &k : kept)
                updateFileEntry(k.path, k.fi);
        if(!toRemove.isEmpty()) {
            // 从高到低 emit，保证视图连续删除时索引始终有效
            std::sort(toRemove.begin(), toRemove.end(),
                      [](const QPair<QString, int> &a, const QPair<QString, int> &b) {
                          return a.second > b.second;
                      });
            if(static_cast<size_t>(toRemove.size()) <= kIncrementalRemoveLimit) {
                // 单条路径：erase + 索引前移 + emit，全程复用既有增量助手
                for(const auto &r : toRemove)
                    removeFileEntry(r.first);
            } else {
                QSet<QString> removalSet;
                for(const auto &r : toRemove) removalSet.insert(r.first);
                std::vector<FSEntry> survivors;
                survivors.reserve(fileEntryVec.size() - static_cast<size_t>(toRemove.size()));
                for(auto &e : fileEntryVec)
                    if(!removalSet.contains(e.path))
                        survivors.push_back(std::move(e));
                fileEntryVec = std::move(survivors);
                rebuildFileIndexMap();
                for(const auto &r : toRemove)
                    emit fileRemoved(r.first, r.second);
            }
        }
    }
}

void DirectoryManager::processPendingAdditions(const QVector<QString> &adds) {
    if(adds.isEmpty())
        return;

    // 去重；已存在的条目改为刷新元数据（覆盖"删除后重建"等场景）
    QVector<QString> paths;
    {
        QSet<QString> seen;
        paths.reserve(adds.size());
        for(const auto &p : adds) {
            if(seen.contains(p)) continue;
            seen.insert(p);
            if(containsFile(p) || containsDir(p)) {
                if(containsFile(p))
                    updateFileEntry(p);
                continue;
            }
            paths.append(p);
        }
    }

    // ⭐ QFileInfo 单次 stat 取齐类型与元数据，替代 directory_entry 构造 + 重复 stat
    std::vector<FSEntry> newFiles;
    std::vector<FSEntry> newDirs;
    for(const auto &p : paths) {
        auto e = FSEntry::fromPath(p);
        if(!e)
            continue;
        if(e->isDirectory) {
            newDirs.push_back(std::move(*e));
        } else {
            // ⭐ 用 basename 取后缀，与目录扫描保持一致，
            // 避免监视目录路径自身含点时误判
            if(!hasSupportedSuffix(e->name, *mSuffixMatcher)) continue;
            newFiles.push_back(std::move(*e));
        }
    }

    // 文件：少量新增走增量插入（避免整表重排），大批量仍批量追加 → 单次排序
    if(!newFiles.empty()) {
        if(newFiles.size() <= kIncrementalAddLimit) {
            for(const auto &e : newFiles) {
                // forceInsertFileEntry 内部即 insert_sorted + 索引平移 + emit fileAdded
                forceInsertFileEntry(e.path, e);
            }
        } else {
            QVector<QString> addedPaths;
            addedPaths.reserve(static_cast<qsizetype>(newFiles.size()));
            for(const auto &e : newFiles) addedPaths.append(e.path);
            fileEntryVec.insert(fileEntryVec.end(),
                                std::make_move_iterator(newFiles.begin()),
                                std::make_move_iterator(newFiles.end()));
            mFilesSorted = false;
            sortFileEntryListsIncremental();
            rebuildFileIndexMap();
            for(const auto &p : addedPaths) emit fileAdded(p);
        }
    }
    if(!newDirs.empty()) {
        if(newDirs.size() <= kIncrementalAddLimit) {
            for(const auto &e : newDirs)
                insertDirEntry(e.path);
        } else {
            dirEntryVec.insert(dirEntryVec.end(),
                               std::make_move_iterator(newDirs.begin()),
                               std::make_move_iterator(newDirs.end()));
            mDirsSorted = false;
            sortDirEntryListsIncremental();
            rebuildDirIndexMap();
        }
    }
}

void DirectoryManager::processPendingModifications(const QSet<QString> &modifies) {
    for(const auto &p : modifies) {
        // ⭐ 只在已存在时更新，避免无意义 filesystem 调用
        if(containsFile(p))
            updateFileEntry(p);
    }
}
