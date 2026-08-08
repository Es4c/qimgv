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
    explicit Cache(size_t maxSize = 20);

    bool contains(const QString &path) const;
    std::shared_ptr<Image> get(const QString &path);
    bool insert(const std::shared_ptr<Image> &img);

    void remove(const QString &path);
    void clear();

private:
    struct Node {
        QString key;
        std::shared_ptr<Image> item;
    };

    using ListIt = std::list<Node>::iterator;

private:
    void moveToFront(ListIt it);
    void evictLRUItems();

private:
    size_t mMaxCacheSize;

    std::list<Node> lruList;                 // front = MRU, back = LRU
    QHash<QString, ListIt> items;

    mutable std::shared_mutex mRWLock;
};
