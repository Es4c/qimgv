#pragma once

#include <QApplication>

class QFileOpenEvent;

class MacOSApplication : public QApplication {
    Q_OBJECT
public:
    MacOSApplication(int &argc, char *argv[]);
protected:
    bool event(QEvent *event) override;
signals:
    void fileOpened(QString filePath);
};
