#pragma once
#include <QtGui/qwindowdefs.h>

// Windows 11 DWM 全屏圆角/边框处理：
// Qt 的全屏窗口实际上只是被放大到屏幕大小，DWM 仍会按普通窗口绘制 1px 边框和圆角，
// 浅色主题下即屏幕四周的白色细线与四个白色圆角。进入全屏时禁用、退出全屏时恢复。
// 进入全屏前的 DWM 状态由调用方持有（FullscreenChromeState），模块内不保存全局状态。
struct FullscreenChromeState {
    quint32 cornerPref = 0;            // DWMWCP_DEFAULT：进入全屏前的圆角偏好
    quint32 borderColor = 0xFFFFFFFFu; // DWMWA_COLOR_DEFAULT：进入全屏前的边框颜色
    bool saved = false;                // 是否已保存过（仅在有保存值时执行恢复）
};

// 进入全屏（fullscreen=true）：保存当前 DWM 圆角偏好/边框颜色并禁用之；
// 退出全屏（fullscreen=false）：用保存的值恢复。未保存过时直接返回，无副作用。
void winSetFullscreenChrome(WId winId, bool fullscreen, FullscreenChromeState& state);
