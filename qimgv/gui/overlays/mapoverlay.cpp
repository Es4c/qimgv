#include "mapoverlay.h"
#include <QPropertyAnimation>
#include <memory>

class MapOverlay::MapOverlayPrivate : public QObject {
public:
    MapOverlayPrivate(MapOverlay *qq);
    ~MapOverlayPrivate();
    void moveInnerWidget(float x, float y);
    void moveMainImage(float xPos, float yPos);

    QPen outlinePen;
    // 缓存画笔/画刷，避免每次 paintEvent 重复构造
    QBrush outerBrush;
    QBrush innerBrush;
    float xSpeedDiff, ySpeedDiff;
    QRectF outerRect, innerRect;
    QRectF windowRect, drawingRect;
    MapOverlay *q;
    int size;
    float opacity;
    float innerOffset;
    int margin;
    // 当前可见状态，用于跳过重复的动画启动
    bool visibleState = false;

    std::unique_ptr<QPropertyAnimation> opacityAnimation;
    MapOverlay::Location location;
};

MapOverlay::MapOverlayPrivate::MapOverlayPrivate(MapOverlay *qq)
    : q(qq), size(120), opacity(0.0f), innerOffset(-1), margin(20),
      outerBrush(QColor(40, 40, 40, 160), Qt::SolidPattern),
      innerBrush(QColor(180, 180, 180, 255), Qt::SolidPattern) {
    outlinePen.setColor(QColor(180, 180, 180, 255));
    location = MapOverlay::RightBottom;
}

MapOverlay::MapOverlayPrivate::~MapOverlayPrivate() = default;

void MapOverlay::MapOverlayPrivate::moveInnerWidget(float x, float y) {
    if(x + static_cast<float>(innerRect.width()) > static_cast<float>(outerRect.right()))
        x = static_cast<float>(outerRect.right() - innerRect.width());

    if(x < 0)
        x = 0;

    if(y + static_cast<float>(innerRect.height()) > static_cast<float>(outerRect.bottom()))
        y = static_cast<float>(outerRect.bottom() - innerRect.height());

    if(y < 0)
        y = 0;

    innerRect.moveTo(QPointF(x, y));
    q->update();
}

void MapOverlay::MapOverlayPrivate::moveMainImage(float xPos, float yPos) {
    float x = static_cast<float>(xPos - (innerRect.width() / 2.0));
    float y = static_cast<float>(yPos - (innerRect.height() / 2.0));

    moveInnerWidget(x, y);

    x /= -xSpeedDiff;
    y /= -ySpeedDiff;

    // Check limits;
    float invisibleX = static_cast<float>(windowRect.width() - drawingRect.width());
    float invisibleY = static_cast<float>(windowRect.height() - drawingRect.height());

    if(x < invisibleX) x = invisibleX;
    if(x > 0) x = 0;

    if(y < invisibleY) y = invisibleY;
    if(y > 0) y = 0;

    emit q->positionChanged(x, y);
}

MapOverlay::MapOverlay(QWidget *parent) : QWidget(parent),
    visibilityEnabled(true),
    d(std::make_unique<MapOverlayPrivate>(this)) {
    this->setMouseTracking(true);
    d->opacityAnimation = std::make_unique<QPropertyAnimation>(this, "opacity");
    d->opacityAnimation->setEasingCurve(QEasingCurve::OutSine);
    d->opacityAnimation->setDuration(150);

    this->setVisible(true);
}

MapOverlay::~MapOverlay() = default;

QSizeF MapOverlay::inner() const {
    return d->innerRect.size();
}

QSizeF MapOverlay::outer() const {
    return d->outerRect.size();
}

float MapOverlay::opacity() const {
    return d->opacity;
}

void MapOverlay::enableVisibility(bool mode) {
    visibilityEnabled = mode;
}

void MapOverlay::setOpacity(float opacity) {
    d->opacity = opacity;
    update();
}

void MapOverlay::animateVisible(bool isVisible) {
    // 状态未变则跳过，避免每次 updateMap 都重启动画
    if(d->visibleState == isVisible)
        return;
    d->visibleState = isVisible;

    if(isVisible) {
        d->opacityAnimation->stop();
        this->setOpacity(1.0f);
    } else {
        d->opacityAnimation->setEndValue(0.0f);
        d->opacityAnimation->start();
    }
}

void MapOverlay::resize(int size) {
    d->size = size;
    QWidget::resize(size, size); // 触发 resizeEvent -> updatePosition()
}

void MapOverlay::setY(int y) {
    move(x(), y);
}

int MapOverlay::y() const {
    return QWidget::y();
}

void MapOverlay::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)
    if(d->opacity <= 0.0f)   // 完全透明时无需绘制
        return;

    QPainter painter(this);
    painter.setOpacity(d->opacity);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    painter.fillRect(d->outerRect, d->outerBrush);
    painter.fillRect(d->innerRect, d->innerBrush);

    painter.setPen(d->outlinePen);
    painter.drawRect(d->outerRect);
}

void MapOverlay::updatePosition() {
    QRect parentRect = parentWidget()->rect();

    int x = 0, y = 0;
    switch(location()) {
        case MapOverlay::LeftTop:
            x = parentRect.left() + margin();
            y = parentRect.top() + margin();
            break;
        case MapOverlay::RightTop:
            x = static_cast<int>(parentRect.right() - (margin() + d->outerRect.width()));
            y = parentRect.top() + margin();
            break;
        case MapOverlay::RightBottom:
            x = static_cast<int>(parentRect.right() - (margin() + d->outerRect.width()));
            y = static_cast<int>(parentRect.bottom() - (margin() + d->outerRect.height()));
            break;
        case MapOverlay::LeftBottom:
            x = parentRect.left() + margin();
            y = static_cast<int>(parentRect.bottom() - (margin() + d->outerRect.height()));
            break;
    }

    /**
     * Save extra space for outer rect border
     * one pixel for right and bottom sides
     */
    setGeometry(x, y, d->size + 1, d->size + 1);
}

namespace {
// 图片是否超出窗口
bool contains(const QRectF &real, const QRectF &expected) {
    return real.width() <= expected.width() && real.height() <= expected.height();
}
}

void MapOverlay::updateMap(const QRectF &drawingRect) {
    if(!isEnabled())
        return;

    const QRectF windowRect = parentWidget()->rect();

    // 绘制区域与窗口矩形均未变时无需重算（避免每帧重复做缩放/除法/重绘）
    if(d->drawingRect == drawingRect && d->windowRect == windowRect)
        return;

    imageDoesNotFit = !contains(drawingRect, windowRect);
    animateVisible(imageDoesNotFit && visibilityEnabled);

    /**
     * Always calculate this first for properly map location
     */
    QSizeF outerSz = drawingRect.size();
    outerSz.scale(d->size, d->size, Qt::KeepAspectRatio);
    d->outerRect.setSize(outerSz);

    d->windowRect = windowRect;
    d->drawingRect = drawingRect;

    float aspect = static_cast<float>(outerSz.width() / drawingRect.width());

    float innerWidth = std::min(static_cast<float>(windowRect.width()) * aspect,
                                static_cast<float>(outerSz.width()));

    float innerHeight = std::min(static_cast<float>(windowRect.height()) * aspect,
                                 static_cast<float>(outerSz.height()));

    QSizeF innerSz(innerWidth, innerHeight);
    d->innerRect.setSize(innerSz);

    d->xSpeedDiff = static_cast<float>(innerSz.width() / windowRect.width());
    d->ySpeedDiff = static_cast<float>(innerSz.height() / windowRect.height());

    float x = static_cast<float>(-drawingRect.left() * d->xSpeedDiff);
    float y = static_cast<float>(-drawingRect.top() * d->ySpeedDiff);

    d->moveInnerWidget(x, y);
    update();
}

void MapOverlay::mousePressEvent(QMouseEvent *event) {
    QWidget::mousePressEvent(event);
    setCursor(Qt::ClosedHandCursor);
    d->moveMainImage(static_cast<float>(event->position().x()),
                     static_cast<float>(event->position().y()));
    event->accept();
}

void MapOverlay::mouseMoveEvent(QMouseEvent *event) {
    QWidget::mouseMoveEvent(event);

    if(event->buttons() & Qt::LeftButton) {
        d->moveMainImage(static_cast<float>(event->position().x()),
                         static_cast<float>(event->position().y()));
    }
    event->accept();
}

void MapOverlay::mouseReleaseEvent(QMouseEvent *event) {
    QWidget::mouseReleaseEvent(event);
    setCursor(Qt::ArrowCursor);
    event->accept();
}

void MapOverlay::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    updatePosition();
}

void MapOverlay::leaveEvent(QEvent *event) {
    Q_UNUSED(event)
    this->enableVisibility(false);
    this->animateVisible(false);
    this->update();
}

void MapOverlay::enterEvent(QEnterEvent *event) {
    Q_UNUSED(event)
    this->enableVisibility(isVisible());
    this->animateVisible(visibilityEnabled && imageDoesNotFit);
    this->update();
}

int MapOverlay::size() const {
    return d->size;
}

MapOverlay::Location MapOverlay::location() const {
    return d->location;
}

void MapOverlay::setLocation(MapOverlay::Location loc) {
    d->location = loc;
}

int MapOverlay::margin() const {
    return d->margin;
}

void MapOverlay::setMargin(int margin) {
    d->margin = margin;
}