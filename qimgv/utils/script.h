#pragma once

#include <QMetaType>
#include <QDataStream>
#include <QString>

class Script {
public:
    Script();
    Script(QString _path, bool _blocking);
    QString command;
    bool blocking;
};

// QSettings 持久化 Script 依赖这两个流操作符；
// Qt6 在注册元类型时自动发现它们，故必须声明在类型之后、Q_DECLARE_METATYPE 之前
QDataStream &operator<<(QDataStream &out, const Script &v);
QDataStream &operator>>(QDataStream &in, Script &v);

Q_DECLARE_METATYPE(Script)
