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
    // 条目改键（O(1)，只动哈希与链表节点，复用条目已记的字节数：不触碰像素数据、
    // 不重新估算、不额外分配）。用于目录扫描完成后把缓存键从用户传入路径改为枚举路径。
    // 冲突策略（本函数保证的不变式）：被改键的条目是权威对象 ——
    // - newPath 不存在：节点原地改键，LRU 位置不变；
    // - newPath 是同一对象的别名：旧键只是同一张图的另一种拼写，丢弃旧键；
    // - newPath 是另一对象（同一文件的另一次加载/预加载）：用被改键的对象覆盖目标条目。
    // 因此返回 true 后，newPath 一定命中调用方正持有/正在显示的那个对象，且只剩一个键。
    // 返回 false 只表示 oldPath 不存在（典型场景是加载仍在途）或 newPath 为空。
    bool rekey(const QString &oldPath, const QString &newPath);
    // ⭐ 缓存键由调用方（目录模型）显式给出，不从 img->filePath() 推导。
    // rekey() 之后条目键是目录枚举路径，而 Image 内部仍保存加载时的用户路径；
    // 若仍按对象路径推导键，一次 updateImage()（旋转/裁剪/丢弃编辑）就会把旧键
    // 重新插回来：同一张图两份条目、内存重复计数，且新键可能指向旧像素。
    // 返回 true = 新增条目；false = 已存在（原地更新）或参数无效
    bool insert(const QString &key, const std::shared_ptr<Image> &img);

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
