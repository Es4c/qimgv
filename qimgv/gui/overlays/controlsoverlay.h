#pragma once

#include <QHBoxLayout>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QDebug>
#include "gui/customwidgets/floatingwidget.h"
#include "gui/customwidgets/actionbutton.h"

class QEnterEvent;

class ControlsOverlay : public FloatingWidget
{
    Q_OBJECT
public:
    explicit ControlsOverlay(FloatingWidgetContainer *parent);
    // 显示但不弹出：保持透明等待悬停，鼠标移到右上角时由 enterEvent 显示
    void showForHover();

private:
    QHBoxLayout *layout;
    ActionButton *minimizeButton, *windowModeButton, *closeButton;
    QGraphicsOpacityEffect *fadeEffect;
    QPropertyAnimation *fadeAnimation;
    QSize contentsSize();
    void fitToContents();
    void recalculateGeometryInternal();
    QSize mCachedContentsSize;

protected:
    void recalculateGeometry() override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
};