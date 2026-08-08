#include "floatingwidget.h"

FloatingWidget::FloatingWidget(FloatingWidgetContainer *parent)
    : QWidget(parent)
    , mAcceptKeyboardFocus(false)
{
    setAccessibleName("OverlayWidget");
    connect(parent, &FloatingWidgetContainer::resized, this, &FloatingWidget::onContainerResized);
    hide();
}

QSize FloatingWidget::containerSize() {
    return container;
}

void FloatingWidget::setContainerSize(QSize newContainer) {
    if(container == newContainer)
        return;
    container = newContainer;
    // 隐藏时无需重算几何；显示前由 showEvent() 补齐
    if(isVisible())
        recalculateGeometry();
}

void FloatingWidget::onContainerResized(QSize size) {
    setContainerSize(size);
}

void FloatingWidget::showEvent(QShowEvent *event) {
    // 隐藏期间容器尺寸可能已变化（setContainerSize 在隐藏时跳过重算），
    // 显示前补齐几何，避免各子类各自处理 show()
    recalculateGeometry();
    QWidget::showEvent(event);
}

void FloatingWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

bool FloatingWidget::acceptKeyboardFocus() const {
    return mAcceptKeyboardFocus;
}

void FloatingWidget::setAcceptKeyboardFocus(bool mode) {
    mAcceptKeyboardFocus = mode;
}

void FloatingWidget::recalculateGeometry() {
}

void FloatingWidget::mousePressEvent(QMouseEvent *event) {
    event->accept();
}

void FloatingWidget::mouseReleaseEvent(QMouseEvent *event) {
    event->accept();
}

void FloatingWidget::wheelEvent(QWheelEvent *event) {
    event->accept();
}

void FloatingWidget::hide() {
    QWidget::hide();
    if(hasFocus() || isAncestorOf(qApp->focusWidget()))
        parentWidget()->setFocus();
}
