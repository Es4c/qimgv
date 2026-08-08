#include "infobarproxy.h"

InfoBarProxy::InfoBarProxy(QWidget *parent) : QWidget(parent), infoBar(nullptr) {
    setAccessibleName("InfoBarProxy");
    setMinimumHeight(23);
    setMaximumHeight(23);
    setAttribute(Qt::WA_StyledBackground, true);
    layout = new QVBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    setLayout(layout);
}

void InfoBarProxy::setInfo(const QString& position, const QString& fileName, const QString& info) {
    if(infoBar) {
        infoBar->setInfo(position, fileName, info);
    } else {
        stateBuf = {position, fileName, info};
    }
}

void InfoBarProxy::init() {
    if(infoBar)
        return;
    infoBar = new InfoBar(this);
    setFocusProxy(infoBar);
    layout->addWidget(infoBar);
    if(!stateBuf.fileName.isEmpty())
        infoBar->setInfo(stateBuf.position, stateBuf.fileName, stateBuf.info);
}