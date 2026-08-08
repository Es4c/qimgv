#pragma once

#include <QPixmap>
#include <QHash>
#include <QMutex>
#include <QtGlobal>
#include <memory>

enum ShrIcon {
    SHR_ICON_ERROR,
    SHR_ICON_LOADING,
    SHR_ICON_COUNT
};

class SharedResources
{
public:
    static SharedResources& getInstance() noexcept;
    ~SharedResources() = default;

    QPixmap& getPixmap(ShrIcon icon, qreal dpr);
    const QPixmap& getPixmap(ShrIcon icon, qreal dpr) const;

private:
    SharedResources() = default;
    Q_DISABLE_COPY(SharedResources)

    // 按量化 DPR 缓存, 避免换屏(不同 DPI)后复用错误的图; 锁保证跨线程安全
    mutable QHash<int, std::unique_ptr<QPixmap>> mIconCache[SHR_ICON_COUNT];
    mutable QMutex mMutex;
};

// 全局引用（inline，不会触发静态初始化警告）
inline SharedResources& shrRes = SharedResources::getInstance();
