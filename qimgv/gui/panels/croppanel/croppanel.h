#pragma once

#include <memory>
#include <QSize>
#include <QRect>
#include <QPointF>

#include "gui/customwidgets/sidepanelwidget.h"

class QTimer;
class QKeyEvent;
class QWheelEvent;
class CropOverlay;

namespace Ui {
class CropPanel;
}

class CropPanel : public SidePanelWidget
{
    Q_OBJECT

public:
    explicit CropPanel(CropOverlay *_overlay, QWidget *parent = nullptr);
    ~CropPanel() override; // 明确标注 override

    void setImageRealSize(QSize size);

public slots:
    void onSelectionOutsideChange(const QRect& rect);
    void show() override;

signals:
    void crop(const QRect& rect);
    void cropAndSave(const QRect& rect);
    void cancel();
    void cropClicked();
    void selectionChanged(const QRect& rect);
    void selectAll();
    void aspectRatioChanged(const QPointF& ratio);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private slots:
    void doCrop();
    void doCropSave();
    void onSelectionChange();
    void flushSelectionSync();   // 节流后的统一同步
    void onAspectRatioChange();   // 响应 SpinBox 手动输入
    void onAspectRatioSelected(); // 响应 ComboBox 下拉选择
    void setFocusCropBtn();
    void setFocusCropSaveBtn();
    void doCropDefaultAction();

private:
    void doCropInternal(bool save);
    std::unique_ptr<Ui::CropPanel> ui; // 使用智能指针管理 UI 生命周期
    std::unique_ptr<QTimer> selectionSyncTimer; // 合并连续 SpinBox 输入
    CropOverlay *overlay;
    QSize realSize;
};