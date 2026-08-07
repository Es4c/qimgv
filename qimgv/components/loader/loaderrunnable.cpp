#include "loaderrunnable.h"
#include "loader.h"
#include "utils/imagefactory.h"
#include <QMetaObject>
#include <utility>

LoaderRunnable::LoaderRunnable(Loader *loader, const QString &path)
    : loader(loader), path(path) {}

void LoaderRunnable::run() {
    markStarted();
    if (cancelled()) {
        // 已被 clearTasks 取消并移出 tasks：释放最后引用，对象随之销毁，不做无用解码
        self.reset();
        return;
    }
    auto image = ImageFactory::createImage(path);
    // 结果经排队连接送回主线程；keepAlive 捕获在事件处理期间维系任务对象存活（无需 deleteLater）
    QMetaObject::invokeMethod(loader,
        [host = loader, p = std::move(path), image = std::move(image), keepAlive = self]() {
            (void)keepAlive;
            host->onLoadFinished(image, p);
        }, Qt::QueuedConnection);
    // 释放本线程持有的引用；事件若尚未处理，则由事件的 keepAlive 维持到处理完
    self.reset();
}
