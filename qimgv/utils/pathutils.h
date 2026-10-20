#pragma once

#include <QDir>
#include <QString>

// "同一路径"判定（跨平台单一定义）。
// 两侧先做词法规范化（QDir::cleanPath：消掉重复分隔符、"."、".."），Windows 再按
// 大小写不敏感比较 —— 同一个文件会以「用户输入的大小写」「目录枚举的真实大小写」
// 「缓存/请求键」三种写法出现，必须按平台规则比较；POSIX 按规范化后的字节比较。
// 注意这是纯词法判定、不解析符号链接：POSIX 上 "link/../a.jpg" 与 "a.jpg" 会被视为
// 同一路径（若 link 指向别的目录，二者其实不同）。因此它只用于同一目录枚举管线内的
// 路径衔接（缓存键、watcher 事件归属、异步回调归属），不能替代 canonicalFilePath()。
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
