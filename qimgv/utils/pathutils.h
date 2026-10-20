#pragma once

#include <QDir>
#include <QString>

// "同一路径"判定（跨平台单一定义）。
// Windows 文件系统大小写不敏感：同一个文件会以「用户输入的大小写」「目录枚举的
// 真实大小写」「缓存/请求键」三种写法出现，必须按平台规则比较；POSIX 仍按字节比较。
// 两侧字符串相同时直接命中、零分配 —— 加载/缩放/列表回调几乎都走这条快路径。
inline bool samePath(const QString &lhs, const QString &rhs)
{
    if (lhs == rhs)
        return true;
#if defined(Q_OS_WIN)
    return QDir::cleanPath(lhs).compare(QDir::cleanPath(rhs), Qt::CaseInsensitive) == 0;
#else
    return QDir::cleanPath(lhs) == QDir::cleanPath(rhs);
#endif
}
