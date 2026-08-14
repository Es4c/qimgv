#pragma once

#ifdef _WIN32
// Windows 11 DWM 全屏圆角/边框处理：
// Qt 的全屏窗口实际上只是被放大到屏幕大小，DWM 仍会按普通窗口绘制 1px 边框和圆角，
// 浅色主题下即屏幕四周的白色细线与四个白色圆角。进入全屏时禁用、退出全屏时恢复。
void winSetFullscreenChrome(void* hwnd, bool fullscreen);
#endif
