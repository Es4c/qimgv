#include "infobar.h"
#include "ui_infobar.h"
#include <QWheelEvent>

InfoBar::InfoBar(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::InfoBar)
{
    setAttribute(Qt::WA_StyledBackground, true);
    ui->setupUi(this);
    ui->path->setText("No file opened.");
}

InfoBar::~InfoBar() {
    delete ui;
}

void InfoBar::setInfo(const QString& position, const QString& fileName, const QString& info) {
    ui->index->setText(position);
    ui->path->setText(fileName);
    ui->info->setText(info);
}

void InfoBar::wheelEvent(QWheelEvent *event) {
    event->accept();
}
