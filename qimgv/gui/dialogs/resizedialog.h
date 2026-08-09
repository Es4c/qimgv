#pragma once

#include <QCheckBox>
#include <QDebug>
#include <QScreen>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace Ui {
    class ResizeDialog;
}

class ResizeDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ResizeDialog(QSize originalSize, QWidget *parent = nullptr);
    ~ResizeDialog();
    QSize newSize();

public slots:
    int exec() override;

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    Ui::ResizeDialog *ui;
    QSize originalSize, targetSize, desktopSize;
    
    void initializeDialog();
    void setupConnections();
    void updateToTargetValues();
    void resetResCheckBox();   // 重置分辨率下拉框

private slots:
    void widthChanged(int);
    void heightChanged(int);
    void percentChanged(double);
    void sizeSelect();

    void setCommonResolution(int);
    void reset();
    void fitDesktop();
    void fillDesktop();
    void onPercentageRadioButton();
    void onAbsoluteSizeRadioButton();
signals:
    void sizeSelected(QSize);
};
