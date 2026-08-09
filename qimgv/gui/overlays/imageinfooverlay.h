#pragma once

#include "gui/customwidgets/overlaywidget.h"
#include "gui/customwidgets/entryinfoitem.h"
#include <QWheelEvent>

namespace Ui {
class ImageInfoOverlay;
}

class ImageInfoOverlay : public OverlayWidget
{
    Q_OBJECT

public:
    explicit ImageInfoOverlay(FloatingWidgetContainer *parent = nullptr);
    ~ImageInfoOverlay();
    void setExifInfo(const QHash<QString, QString>&);

public slots:
    void show() override;

protected:
    void wheelEvent(QWheelEvent *event) override;
private:
    Ui::ImageInfoOverlay *ui;
    QList<EntryInfoItem*> entries;
    QLabel entryStub;
    QHash<QString, QString> m_lastInfo;   // 上次展示的信息，用于跳过重复刷新
};
