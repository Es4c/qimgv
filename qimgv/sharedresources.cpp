#include "sharedresources.h"

#include <array>

// 资源路径常量（避免重复构造 QString）
namespace {
    constexpr const char* PATH_LOADING      = ":/res/icons/common/other/loading72.png";
    constexpr const char* PATH_LOADING_2X   = ":/res/icons/common/other/loading72@2x.png";

    constexpr const char* PATH_ERROR        = ":/res/icons/common/other/loading-error72.png";
    constexpr const char* PATH_ERROR_2X     = ":/res/icons/common/other/loading-error72@2x.png";

    // 表驱动：按图标枚举直接索引, 消除重复分支
    struct IconSpec {
        const char* path1x;
        const char* path2x;
    };

    constexpr std::array<IconSpec, SHR_ICON_COUNT> kIconSpecs = {{
        { PATH_ERROR,    PATH_ERROR_2X },    // SHR_ICON_ERROR
        { PATH_LOADING,  PATH_LOADING_2X },  // SHR_ICON_LOADING
    }};
}

SharedResources& SharedResources::getInstance() noexcept
{
    static SharedResources instance;
    return instance;
}

QPixmap& SharedResources::getPixmap(ShrIcon icon, qreal dpr)
{
    QMutexLocker lock(&mMutex);

    int idx = static_cast<int>(icon);
    if (idx < 0 || idx >= SHR_ICON_COUNT)
        idx = SHR_ICON_ERROR;

    // DPR 判断（避免重复比较）; <1.999 的分数倍率统一用 2x 图
    const bool highDpr = (dpr >= 1.001);
    const qreal targetDpr = highDpr ? ((dpr >= 1.999) ? dpr : 2.0) : 1.0;

    // 按实际生效 DPR 量化缓存, 换屏后自动取对应倍率的图
    const int key = qRound(targetDpr * 100.0);

    auto &slot = mIconCache[idx];
    auto it = slot.constFind(key);
    if (it == slot.cend()) {
        const IconSpec &spec = kIconSpecs[idx];
        auto pixmap = std::make_unique<QPixmap>(QString::fromLatin1(highDpr ? spec.path2x : spec.path1x));

        if (!pixmap->isNull() && targetDpr != 1.0)
            pixmap->setDevicePixelRatio(targetDpr);

        slot.insert(key, std::move(pixmap));
        it = slot.constFind(key);
    }
    return **it;
}

const QPixmap& SharedResources::getPixmap(ShrIcon icon, qreal dpr) const
{
    return const_cast<SharedResources*>(this)->getPixmap(icon, dpr);
}
