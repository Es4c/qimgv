#include "win11window.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>

// dwmapi.h 较老版本可能没有 Windows 11 (build 22000) 才引入的常量，缺什么补什么
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWCP_DEFAULT
#define DWMWCP_DEFAULT 0
#endif
#ifndef DWMWCP_DONOTROUND
#define DWMWCP_DONOTROUND 1
#endif
#ifndef DWMWA_COLOR_NONE
#define DWMWA_COLOR_NONE 0xFFFFFFFEu
#endif
#ifndef DWMWA_COLOR_DEFAULT
#define DWMWA_COLOR_DEFAULT 0xFFFFFFFFu
#endif

void winSetFullscreenChrome(void* hwnd, bool fullscreen) {
    if (!hwnd)
        return;

    // 全屏前保存的圆角偏好，退出全屏时恢复（主窗口仅此一个实例，函数内 static 足够）
    static DWORD savedCornerPref = DWMWCP_DEFAULT;

    const HWND native = static_cast<HWND>(hwnd);
    const DWORD attrSize = static_cast<DWORD>(sizeof(DWORD));

    if (fullscreen) {
        // 记录进入全屏前的圆角偏好，供退出全屏时恢复
        DWORD pref = DWMWCP_DEFAULT;
        DwmGetWindowAttribute(native, DWMWA_WINDOW_CORNER_PREFERENCE,
                              &pref, attrSize);
        savedCornerPref = pref;

        // 全屏时禁用 DWM 圆角，避免屏幕四角露出白色圆角
        pref = DWMWCP_DONOTROUND;
        DwmSetWindowAttribute(native, DWMWA_WINDOW_CORNER_PREFERENCE,
                              &pref, attrSize);
        // 禁用 DWM 的 1px 边框（浅色主题下默认是白色细线）
        const DWORD noBorder = DWMWA_COLOR_NONE;
        DwmSetWindowAttribute(native, DWMWA_BORDER_COLOR,
                              &noBorder, attrSize);
    } else {
        // 恢复窗口化时的圆角偏好与默认边框
        const DWORD pref = savedCornerPref;
        DwmSetWindowAttribute(native, DWMWA_WINDOW_CORNER_PREFERENCE,
                              &pref, attrSize);
        const DWORD defaultBorder = DWMWA_COLOR_DEFAULT;
        DwmSetWindowAttribute(native, DWMWA_BORDER_COLOR,
                              &defaultBorder, attrSize);
    }
}

#endif // _WIN32
