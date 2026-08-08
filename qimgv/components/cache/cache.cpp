#include "cache.h"

Cache::Cache(size_t maxSize)
    : mMaxCacheSize(maxSize)
{
}

bool Cache::contains(const QString &path) const {
    std::shared_lock lock(mRWLock);
    return items.contains(path);
}

std::shared_ptr<Image> Cache::get(const QString &path) {
    std::unique_lock lock(mRWLock);

    auto it = items.find(path);
    if (it == items.end()) return nullptr;

    // 命中即提升到 MRU 头，保证淘汰时 LRU 顺序始终新鲜
    moveToFront(it.value());
    return it.value()->item;
}

bool Cache::insert(const std::shared_ptr<Image> &img) {
    if (!img) return false;

    std::unique_lock lock(mRWLock);

    const QString &path = img->filePath();

    auto it = items.find(path);
    if (it != items.end()) {
        // 原地更新，保持原有返回语义（false = 已存在，非新增）
        it.value()->item = img;
        moveToFront(it.value());
        return false;
    }

    lruList.push_front({path, img});
    items.insert(path, lruList.begin());

    if (items.size() > mMaxCacheSize) {
        evictLRUItems();
    }

    return true;
}

void Cache::remove(const QString &path) {
    std::unique_lock lock(mRWLock);

    auto it = items.find(path);
    if (it == items.end()) return;

    lruList.erase(it.value());
    items.erase(it);
}

void Cache::clear() {
    std::unique_lock lock(mRWLock);
    lruList.clear();
    items.clear();
}

void Cache::moveToFront(ListIt it) {
    if (it == lruList.begin()) return;
    lruList.splice(lruList.begin(), lruList, it);
}

void Cache::evictLRUItems() {
    while (items.size() > mMaxCacheSize) {
        auto lastIt = std::prev(lruList.end());

        items.remove(lastIt->key);
        lruList.pop_back(); // O(1)
    }
}
