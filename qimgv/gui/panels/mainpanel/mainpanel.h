#pragma once
#include <QWidget>
#include <QVBoxLayout>
#include "gui/customwidgets/slidepanel.h"
#include "gui/customwidgets/actionbutton.h"

class MainPanel : public SlidePanel {
Q_OBJECT
public:
MainPanel(FloatingWidgetContainer *parent);
~MainPanel();
void setPosition(PanelPosition);
void setExitButtonEnabled(bool mode);
QSize sizeHint() const override;
public slots:
void readSettings();
signals:
void pinned(bool mode);
private slots:
void onPinClicked(bool checked);
private:
QVBoxLayout *buttonsLayout;
QWidget buttonsWidget;
    ActionButton *openButton, *settingsButton, *exitButton, *pinButton;
};