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
    // show()/hide() 必须为虚函数：QWidget 的 show() 不是虚的，若此处不声明为虚，
    // 通过 FloatingWidget* 调用会绕过子类实现（如 CropOverlay 的 mHiddenByHide
    // 复位、OverlayWidget 的淡出处理），导致隐藏标记/动画状态不同步
    virtual void show();
    virtual void hide();

protected:
    // called whenever container rectangle changes
    // this does nothing, reimplement to use
    virtual void recalculateGeometry();
    void showEvent(QShowEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void setContainerSize(QSize newContainer);

    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
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
