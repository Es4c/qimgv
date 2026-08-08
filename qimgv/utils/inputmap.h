#pragma once

#include <QHash>
#include <QString>

class InputMap {
public:
    InputMap();
    static InputMap *getInstance();
    // 按 scancode 查键名; 找不到返回空串
    QString keyNameForScancode(quint32 scanCode) const;
    const QHash<QString, Qt::KeyboardModifier> &modifiers();
    static const QString& keyNameCtrl();
    static const QString& keyNameAlt();
    static const QString& keyNameShift();

private:
    void initKeyMap();
    void initModMap();
    QHash<quint32, QString> keyMap;
    QHash<QString, Qt::KeyboardModifier> modMap;
};

extern InputMap *inputMap;
