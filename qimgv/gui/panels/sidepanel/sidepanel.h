#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include "gui/customwidgets/sidepanelwidget.h"

namespace Ui {
class SidePanel;
}

class SidePanel : public QWidget
{
    Q_OBJECT

public:
    explicit SidePanel(QWidget *parent = nullptr);
    ~SidePanel();

    void setWidget(SidePanelWidget *w);
    SidePanelWidget* widget();

public slots:
    void show();
    void hide();

private:
    Ui::SidePanel *ui;
    SidePanelWidget *mWidget;
};
