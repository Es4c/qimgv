#include "imageinfooverlayproxy.h"

ImageInfoOverlayProxy::ImageInfoOverlayProxy(FloatingWidgetContainer *parent)
    : container(parent),
      overlay(nullptr)
{
}

ImageInfoOverlayProxy::~ImageInfoOverlayProxy() {
    if(overlay)
        overlay->deleteLater();
}

void ImageInfoOverlayProxy::show() {
    init();
    // 无论信息是否为空都要同步给 overlay：空信息同样要刷新（渲染
    // "<no metadata found>"），否则隐藏期间切到无元数据图片再打开时，
    // 会残留上一张图的旧条目（overlay 内部对非空且未变的信息会早退）
    overlay->setExifInfo(stateBuf.info);
    overlay->show();
}

void ImageInfoOverlayProxy::hide() {
    if(overlay)
        overlay->hide();
}

void ImageInfoOverlayProxy::init() {
    if(overlay)
        return;
    overlay = new ImageInfoOverlay(container);
    overlay->setExifInfo(stateBuf.info);
}

bool ImageInfoOverlayProxy::isHidden() {
    return overlay ? overlay->isHidden() : true;
}

void ImageInfoOverlayProxy::setExifInfo(const QHash<QString, QString>& _info) {
    if(stateBuf.info == _info)
        return;   // 内容未变则跳过拷贝与刷新
    stateBuf.info = _info;
    if(overlay && !overlay->isHidden())
        overlay->setExifInfo(_info);
}
