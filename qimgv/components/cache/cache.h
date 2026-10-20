#pragma once

#include <QHash>
#include <QString>
#include <list>
#include <memory>
#include <mutex>
#include <shared_mutex>

#include "sourcecontainers/image.h"

class Cache {
public:
    // maxSize 为个数上限（兼容旧语义）；字节上限默认 2048MB，避免高像素 OOM；
    // 超上限的单张大图允许独占一项（否则永远 miss）；maxSize == 0 视为禁用
    explicit Cache(size_t maxSize = 20, size_t maxBytes = 2048ULL * 1024ULL * 1024ULL);

    bool contains(const QString &path) const;
    std::shared_ptr<Image> get(const QString &path);
    // 单锁取出并删除，用于 modify 路径合并 3 次加锁为 1 次
    std::shared_ptr<Image> take(const QString &path);
    // 条目改键（O(1)，只动哈希与链表节点，不触碰像素数据）。用于目录扫描完成后
    // 把缓存键从用户传入路径改为枚举路径；oldPath 不存在或 newPath 已被占用时
    // 返回 false 且不做任何改动
    bool rekey(const QString &oldPath, const QString &newPath);
    bool insert(const std::shared_ptr<Image> &img);

    void remove(const QString &path);
    void clear();

    void setMaxBytes(size_t bytes) { mMaxBytes = bytes; }

private:
    struct Node {
        QString key;
        std::shared_ptr<Image> item;
        size_t bytes = 0;
    };

    using ListIt = std::list<Node>::iterator;

private:
    void moveToFront(ListIt it);
    void evictLRUItems();
    static size_t estimateBytes(const std::shared_ptr<Image> &img) noexcept;

private:
    size_t mMaxCacheSize;
    size_t mMaxBytes;
    size_t mCurrentBytes = 0;

    std::list<Node> lruList;                 // front = MRU, back = LRU
    QHash<QString, ListIt> items;

    mutable std::shared_mutex mRWLock;
};
