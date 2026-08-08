#pragma once

#include <QWidget>

class QWheelEvent;

namespace Ui {
class InfoBar;
}

class InfoBar : public QWidget
{
    Q_OBJECT

public:
    explicit InfoBar(QWidget *parent = nullptr);
    ~InfoBar();

public slots:
    void setInfo(const QString& position, const QString& fileName, const QString& info);
protected:
    void wheelEvent(QWheelEvent *event) override;
private:
    Ui::InfoBar *ui;
};
