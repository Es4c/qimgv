#pragma once
#include <QVersionNumber>

// 内联变量 + 常量初始化: 无静态初始化顺序问题, 且省去函数局部 static + 引用的间接层
// NOLINTNEXTLINE(bugprone-throwing-static-initialization) — 常量入参构造不会抛异常
inline const QVersionNumber appVersion(1, 0, 3);
