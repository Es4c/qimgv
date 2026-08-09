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
    bool showVideo(QString file) override;
    void seek(int pos) override;
    void seekRelative(int pos) override;
    void pauseResume() override;
    void frameStep() override;
    void frameStepBack() override;
    void stop() override;
    void setPaused(bool mode) override;
    void setMuted(bool) override;
    bool muted() override;
    void volumeUp() override;
    void volumeDown() override;
    void setVolume(int) override;
    int volume() override;
    void setVideoUnscaled(bool mode) override;
    void setLoopPlayback(bool mode) override;
    VideoPlayer* getPlayer();
    bool isInitialized();

    void installEventFilter(QObject *filterObj);
    void removeEventFilter(QObject *filterObj);

public slots:
    void show() override;
    void hide() override;

protected:
    void paintEvent(QPaintEvent *event) override;

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
