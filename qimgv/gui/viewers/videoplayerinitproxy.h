// performs lazy initialization

#pragma once

#include <QVBoxLayout>
#include "videoplayer.h"
#include "settings.h"
#include <QPainter>
#include <QLibrary>
#include <QLabel>
#include <QFileInfo>
#include <QDebug>
#include <QPointer>

class VideoPlayerInitProxy : public VideoPlayer {
public:
    VideoPlayerInitProxy(QWidget *parent = nullptr);
    ~VideoPlayerInitProxy() = default;
    bool showVideo(QString file);
    void seek(int pos);
    void seekRelative(int pos);
    void pauseResume();
    void frameStep();
    void frameStepBack();
    void stop();
    void setPaused(bool mode);
    void setMuted(bool);
    bool muted();
    void volumeUp();
    void volumeDown();
    void setVolume(int);
    int volume();
    void setVideoUnscaled(bool mode);
    void setLoopPlayback(bool mode);
    VideoPlayer* getPlayer();
    bool isInitialized();

    void installEventFilter(QObject *filterObj);
    void removeEventFilter(QObject *filterObj);

public slots:
    void show();
    void hide();

protected:
    void paintEvent(QPaintEvent *event);

private:
    QLibrary playerLib;
    QPointer<VideoPlayer> player;
    bool initPlayer();
    QVBoxLayout *layout;
    QLabel *errorLabel = nullptr;
    QObject *eventFilterObj = nullptr;

    QString libFile;
    QStringList libDirs;
    bool playerLibInitialized = false;
    bool playerInitFailed = false;

private slots:
    void onSettingsChanged();

};
