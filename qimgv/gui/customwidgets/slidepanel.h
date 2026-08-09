#pragma once

#include <QBoxLayout>
#include "floatingwidget.h"
#include "settings.h"

class SlidePanel : public FloatingWidget {
    Q_OBJECT
public:
    explicit SlidePanel(FloatingWidgetContainer *parent);
    ~SlidePanel();

    QRect triggerRect();
    bool layoutManaged();
    void setLayoutManaged(bool mode);

    void setPosition(PanelPosition);
    PanelPosition position();

public slots:
    void show() override;
    void hide() override;

protected:
    QHBoxLayout *mLayout;
    QRect mTriggerRect;
    PanelPosition mPosition;

    // 保持虚函数接口，供容器 resize 时定位
    void recalculateGeometry() override;

private:
    bool mLayoutManaged = false;
};
