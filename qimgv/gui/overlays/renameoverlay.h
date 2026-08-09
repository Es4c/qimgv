#pragma once

#include <QPushButton>
#include <QLineEdit>
#include <QKeyEvent>
#include <QTimer>

#include "gui/customwidgets/overlaywidget.h"
#include "components/actionmanager/actionmanager.h"
#include "settings.h"

namespace Ui {
class RenameOverlay;
}

class RenameOverlay : public OverlayWidget
{
    Q_OBJECT

public:
    explicit RenameOverlay(FloatingWidgetContainer *parent);
    ~RenameOverlay();

public slots:
    void setName(const QString &name);
    void setBackdropEnabled(bool mode);
    void show() override;
    void hide() override;
signals:
    void renameRequested(QString name);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void recalculateGeometry() override;

private slots:
    void rename();
    void onCancel();

private:
    Ui::RenameOverlay *ui;
    bool backdrop = false;
    QString origName;
    QSet<QString> keyFilter;
    void selectName();
};
