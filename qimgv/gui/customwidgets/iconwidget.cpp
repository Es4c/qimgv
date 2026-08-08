#include "iconwidget.h"

#include <QHash>
#include <QEvent>
#include <QShowEvent>

namespace {
    // 共享图标缓存：同一 (路径, dpr, 颜色) 只解码/着色一次，
    // 避免每个图标实例重复从资源加载并重复做全图填充重着色
    using IconCache = QHash<QString, std::shared_ptr<QPixmap>>;

    IconCache& iconCache() {
        static IconCache cache;
        return cache;
    }

    // 缓存键：路径 + dpr + 着色参数。SOURCE 模式不着色，使用固定后缀
    QString cacheKey(const QString &path, qreal dpr, IconColorMode mode, const QColor &color) {
        const QString colorPart = (mode == ICON_COLOR_SOURCE)
                                      ? QStringLiteral("-")
                                      : color.name(QColor::HexArgb);
        return path + QLatin1Char('\n') + QString::number(dpr) + QLatin1Char('\n') + colorPart;
    }
}

IconWidget::IconWidget(QWidget *parent)
    : QWidget(parent)
{
    dpr = devicePixelRatioF();
    color = settings->colorScheme().icons;
    connect(settings, &Settings::settingsChanged, this, &IconWidget::onSettingsChanged);
}

IconWidget::~IconWidget() = default; // std::shared_ptr 会自动释放内存

void IconWidget::onSettingsChanged() {
    if(colorMode == ICON_COLOR_THEME) {
        const QColor newColor = settings->colorScheme().icons;
        if(color != newColor) {
            color = newColor;
            applyColor();
        }
    }
}

void IconWidget::setIconPath(const QString &path) {
    if(iconPath == path)
        return;
    iconPath = path;
    loadIcon();
}

void IconWidget::loadIcon() {
    auto path = iconPath;

    if(dpr >= (1.0 + 0.001)) {
        // 只替换扩展名前的最后一个点，避免路径中其他点被误伤
        const qsizetype dot = path.lastIndexOf(QLatin1Char('.'));
        if(dot >= 0)
            path.replace(dot, 1, "@2x.");
        hiResPixmap = true;
        pixmapDrawScale = (dpr >= (2.0 - 0.001)) ? dpr : 2.0;
    } else {
        hiResPixmap = false;
        pixmapDrawScale = dpr;
    }

    const QString key = cacheKey(iconPath, dpr, ICON_COLOR_SOURCE, QColor());
    IconCache &cache = iconCache();
    auto it = cache.find(key);
    if(it == cache.end()) {
        auto loaded = std::make_shared<QPixmap>(path);
        if(loaded->isNull()) {
            rawPixmap.reset();
            pixmap.reset();
            update();
            return;
        }
        loaded->setDevicePixelRatio(pixmapDrawScale);
        it = cache.insert(key, std::move(loaded));
    }
    rawPixmap = it.value();
    pixmap = rawPixmap;
    if(colorMode == ICON_COLOR_SOURCE)
        update(); // SOURCE 模式不着色，applyColor 会直接返回，需手动刷新
    else
        applyColor(); // applyColor 内部已触发 update()
}

void IconWidget::updateDpr() {
    const qreal newDpr = devicePixelRatioF();
    if(qFuzzyCompare(newDpr, dpr))
        return;
    dpr = newDpr;
    loadIcon(); // 按新 dpr 重新走 @2x 分支并取缓存
}

void IconWidget::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    // 构造时窗口尚未在屏上，devicePixelRatioF() 常为 1.0；
    // 首次真正显示时才拿到正确 dpr
    updateDpr();
}

bool IconWidget::event(QEvent *event) {
    const bool handled = QWidget::event(event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    // 窗口跨屏拖动 / 系统缩放变化时 dpr 会变，按新 dpr 重新取图
    if(event->type() == QEvent::DevicePixelRatioChange)
        updateDpr();
#endif
    return handled;
}

QSize IconWidget::minimumSizeHint() const {
    if(pixmap && !pixmap->isNull())
        return pixmap->size() / dpr;
    
    return QWidget::minimumSizeHint();
}

void IconWidget::setIconOffset(const QPoint &offset) {
    iconOffset = offset;
    update();
}

void IconWidget::setIconOffset(int x, int y) {
    setIconOffset(QPoint(x, y));
}

void IconWidget::setColorMode(IconColorMode mode) {
    if(colorMode != mode && mode == ICON_COLOR_SOURCE) {
        colorMode = mode;
        loadIcon();
    } else {
        colorMode = mode;
        applyColor();
    }
}

void IconWidget::setColor(QColor color) {
    this->colorMode = ICON_COLOR_CUSTOM;
    this->color = color;
    applyColor();
}

void IconWidget::applyColor() {
    if(!rawPixmap || rawPixmap->isNull() || colorMode == ICON_COLOR_SOURCE)
        return;

    const QString key = cacheKey(iconPath, dpr, colorMode, color);
    IconCache &cache = iconCache();
    auto it = cache.find(key);
    if(it != cache.end()) {
        pixmap = it.value();
        update();
        return;
    }

    // 从原始图复制一份再着色，避免改动共享的缓存数据
    auto recolored = std::make_shared<QPixmap>(rawPixmap->copy());
    if(recolored->isNull())
        return;
    recolored->setDevicePixelRatio(pixmapDrawScale);
    ImageLib::recolor(*recolored, color);
    pixmap = cache.insert(key, std::move(recolored)).value();
    update();
}

void IconWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)
    QPainter p(this);
    if(!isEnabled())
        p.setOpacity(0.5f);

    QStyleOption opt;
    opt.initFrom(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);

    if(pixmap) {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        QPointF pos;
        // 修复 bugprone-integer-division：使用 2.0 强制进入浮点运算
        if(hiResPixmap) {
            pos = QPointF(width() / 2.0 - pixmap->width() / (2.0 * pixmapDrawScale),
                          height() / 2.0 - pixmap->height() / (2.0 * pixmapDrawScale));
        } else {
            pos = QPointF(width() / 2.0 - pixmap->width() / 2.0,
                          height() / 2.0 - pixmap->height() / 2.0);
        }
        p.drawPixmap(pos + iconOffset, *pixmap);
    }
}