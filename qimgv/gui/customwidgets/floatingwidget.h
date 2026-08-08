/*
 * Base class for floating panels / overlay widgets.
 * It is not supposed to go into any kind of layout, just set parent & call show().
 * Usage: reimplement recalculateGeometry() method, which sets the new
 * geometry when the parent is resized.
 */

#pragma once

#include "gui/customwidgets/floatingwidgetcontainer.h"
#include <QStyleOption>
#include <QPainter>
#include <QApplication>

#include <QWheelEvent>

class FloatingWidget : public QWidget
{
    Q_OBJECT
public:
    explicit FloatingWidget(FloatingWidgetContainer *parent);
    QSize containerSize();
    bool acceptKeyboardFocus() const;
    void setAcceptKeyboardFocus(bool mode);

public slots:
    void hide();

protected:
    // called whenever container rectangle changes
    // this does nothing, reimplement to use
    virtual void recalculateGeometry();
    void showEvent(QShowEvent *event) override;
    void paintEvent(QPaintEvent *event);
    void setContainerSize(QSize newContainer);

    void mousePressEvent(QMouseEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);
    void wheelEvent(QWheelEvent *event);
private:
    // size of whatever widget we are overlayed on
    QSize container;
    bool mAcceptKeyboardFocus = false;
    // 几何是否已过期：隐藏期间容器变化 / 首次显示前为 true，
    // 由 showEvent() 在真正重算后清除，避免每次 show 都重复 setGeometry
    bool mGeometryDirty = true;

private slots:
    void onContainerResized(QSize container);
};
