#pragma once

#include <QHBoxLayout>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QDebug>
#include "gui/customwidgets/floatingwidget.h"
#include "gui/customwidgets/actionbutton.h"

class ControlsOverlay : public FloatingWidget
{
    Q_OBJECT
public:
    explicit ControlsOverlay(FloatingWidgetContainer *parent);

public slots:
    void show() override;

private:
    QHBoxLayout *layout;
    ActionButton *minimizeButton, *windowModeButton, *closeButton;
    QGraphicsOpacityEffect *fadeEffect;
    QPropertyAnimation *fadeAnimation;
    QPropertyAnimation *fadeInAnimation;
    QSize contentsSize();
    void fitToContents();
    void recalculateGeometryInternal();
    QSize mCachedContentsSize;

protected:
    void recalculateGeometry() override;
#if QT_VERSION > QT_VERSION_CHECK(6,0,0)
    void enterEvent(QEnterEvent *event) override;
#else
    void enterEvent(QEvent *event) override;
#endif
    void leaveEvent(QEvent *event) override;
};