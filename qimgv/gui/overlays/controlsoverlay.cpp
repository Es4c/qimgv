#include "controlsoverlay.h"

ControlsOverlay::ControlsOverlay(FloatingWidgetContainer *parent) :
    FloatingWidget(parent)
{
    minimizeButton = new ActionButton("minimize", ":/res/icons/common/buttons/panel/minimize16.png", 30);
    minimizeButton->setAccessibleName("ButtonSmall");
    windowModeButton = new ActionButton("toggleFullscreen", ":/res/icons/common/buttons/panel/maximize16.png", 30);
    windowModeButton->setAccessibleName("ButtonSmall");
    closeButton = new ActionButton("exit", ":/res/icons/common/buttons/panel/close16.png", 30);
    closeButton->setAccessibleName("ButtonSmall");

    layout = new QHBoxLayout();
    layout->setContentsMargins(0,0,0,0);
    this->setContentsMargins(0,0,0,0);
    layout->setSpacing(0);
    layout->addWidget(minimizeButton);
    layout->addWidget(windowModeButton);
    layout->addWidget(closeButton);
    setLayout(layout);
    
    // 构造函数中直接设置大小和几何，避免调用虚函数
    mCachedContentsSize = QSize(0, 0);
    for(int i=0; i<layout->count(); i++) {
        mCachedContentsSize.setWidth(mCachedContentsSize.width() + layout->itemAt(i)->widget()->width());
        mCachedContentsSize.setHeight(layout->itemAt(i)->widget()->height());
    }
    this->setFixedSize(mCachedContentsSize);
    recalculateGeometryInternal();

    setMouseTracking(true);

    fadeEffect = new QGraphicsOpacityEffect(this);
    this->setGraphicsEffect(fadeEffect);
    fadeAnimation = new QPropertyAnimation(fadeEffect, "opacity");
    fadeAnimation->setDuration(230);
    fadeAnimation->setStartValue(1.0f);
    fadeAnimation->setEndValue(0);
    fadeAnimation->setEasingCurve(QEasingCurve::OutQuart);

    // 淡入动画：show() 时从透明淡入，避免之前显示后不可见的 bug
    fadeInAnimation = new QPropertyAnimation(fadeEffect, "opacity");
    fadeInAnimation->setDuration(230);
    fadeInAnimation->setStartValue(0.0f);
    fadeInAnimation->setEndValue(1.0f);
    fadeInAnimation->setEasingCurve(QEasingCurve::OutQuart);

    if(parent)
        setContainerSize(parent->size());
    //this->show();
}

void ControlsOverlay::show() {
    // 已完全可见且无动画进行时，重复 show（如反复进全屏）不再重放淡入
    if(isVisible() && fadeEffect->opacity() >= 1.0f
       && fadeAnimation->state() == QAbstractAnimation::Stopped
       && fadeInAnimation->state() == QAbstractAnimation::Stopped)
        return;
    fadeEffect->setOpacity(0.0);
    fadeAnimation->stop();
    fadeInAnimation->stop();
    fadeInAnimation->start();   // 从透明淡入
    FloatingWidget::show();
}

QSize ControlsOverlay::contentsSize() {
    return mCachedContentsSize;
}

void ControlsOverlay::fitToContents() {
    this->setFixedSize(mCachedContentsSize);
    recalculateGeometry();
}

void ControlsOverlay::recalculateGeometryInternal() {
    setGeometry(containerSize().width() - width(), 0, width(), height());
}

void ControlsOverlay::recalculateGeometry() {
    recalculateGeometryInternal();
}

void ControlsOverlay::enterEvent(QEnterEvent *event) {
    Q_UNUSED(event)
    fadeAnimation->stop();
    fadeInAnimation->stop();
    fadeEffect->setOpacity(1.0);
}

void ControlsOverlay::leaveEvent(QEvent *event) {
    Q_UNUSED(event)
    fadeInAnimation->stop();
    // 从当前透明度淡出，避免快速进出时跳变
    fadeAnimation->setStartValue(fadeEffect->opacity());
    fadeAnimation->start();
}