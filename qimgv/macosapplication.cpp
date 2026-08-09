#include "macosapplication.h"

#include <QFileOpenEvent>

MacOSApplication::MacOSApplication(int &argc, char *argv[]) : QApplication(argc, argv) {}

bool MacOSApplication::event(QEvent *event) {
    // macOS 下通过 Finder / Dock 拖拽打开文件时投递 FileOpen 事件
    if (event->type() == QEvent::FileOpen) {
        emit fileOpened(static_cast<QFileOpenEvent *>(event)->file());
        return true;
    }
    return QApplication::event(event);
}
