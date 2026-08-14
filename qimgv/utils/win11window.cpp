#include "win11window.h"

#include <bit>

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
#ifndef DWMWCP_DONOTROUND
#define DWMWCP_DONOTROUND 1
#endif
#ifndef DWMWA_COLOR_NONE
#define DWMWA_COLOR_NONE 0xFFFFFFFEu
#endif

// 禁用 DWM 圆角与 1px 边框（幂等）
static void disableFullscreenChrome(HWND hwnd, DWORD attrSize) {
    const DWORD noRound = DWMWCP_DONOTROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                          &noRound, attrSize);
    const DWORD noBorder = DWMWA_COLOR_NONE;
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR,
                          &noBorder, attrSize);
}

void winSetFullscreenChrome(WId winId, bool fullscreen, FullscreenChromeState& state) {
    // Qt 的 winId() 返回整数（WId=quintptr），HWND 是指针类型；
    // 用 std::bit_cast 按位转换，避免 reinterpret_cast 触发
    // clang-tidy 的 performance-no-int-to-ptr 告警
    const HWND hwnd = std::bit_cast<HWND>(winId);
    if (!hwnd)
        return;

    constexpr DWORD attrSize = sizeof(DWORD);

    if (fullscreen) {
        // 记录进入全屏前的 DWM 状态，供退出全屏时恢复
        DwmGetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                              &state.cornerPref, attrSize);
        DwmGetWindowAttribute(hwnd, DWMWA_BORDER_COLOR,
                              &state.borderColor, attrSize);
        state.saved = true;

        // 全屏时禁用 DWM 圆角，避免屏幕四角露出白色圆角；同时去掉 1px 边框
        disableFullscreenChrome(hwnd, attrSize);
    } else if (state.saved) {
        // 恢复窗口化时的圆角偏好与边框颜色
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                              &state.cornerPref, attrSize);
        DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR,
                              &state.borderColor, attrSize);
        state.saved = false;
    }
}

void winDisableFullscreenChrome(WId winId) {
    const HWND hwnd = std::bit_cast<HWND>(winId);
    if (!hwnd)
        return;

    disableFullscreenChrome(hwnd, sizeof(DWORD));
}
