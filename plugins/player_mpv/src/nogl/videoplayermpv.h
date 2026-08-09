#pragma once

#include "videoplayer.h"
#include <QKeyEvent>

#if defined QIMGV_PLAYER_MPV_LIBRARY
 #define TEST_COMMON_DLLSPEC Q_DECL_EXPORT
#else
 #define TEST_COMMON_DLLSPEC Q_DECL_IMPORT
#endif

class MpvWidget;

class VideoPlayerMpv final : public VideoPlayer {
    Q_OBJECT
public:
    explicit VideoPlayerMpv(QWidget *parent = nullptr);
    [[nodiscard]] bool showVideo(const QString &file);
    void setVideoUnscaled(bool mode);
    [[nodiscard]] int volume() const;

public slots:
    void seek(int pos);
    void seekRelative(int pos);
    void pauseResume();
    void frameStep();
    void frameStepBack();
    void stop();
    void setPaused(bool mode);
    void setMuted(bool mode);
    [[nodiscard]] bool muted() const;
    void volumeUp();
    void volumeDown();
    void setVolume(int vol);
    void setLoopPlayback(bool mode);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    MpvWidget *m_mpv;

};

extern "C" TEST_COMMON_DLLSPEC VideoPlayer *CreatePlayerWidget();
