#include "videoplayermpv.h"
#include "mpvwidget.h"
#include <QLayout>
#include <string>

// TODO: window flashes white when opening a video (straight from file manager)
VideoPlayerMpv::VideoPlayerMpv(QWidget *parent) : VideoPlayer(parent) {
    setAttribute(Qt::WA_TranslucentBackground, true);
    setMouseTracking(true);

    m_mpv = new MpvWidget(this);
    QVBoxLayout *vl = new QVBoxLayout();
    vl->setContentsMargins(0,0,0,0);
    vl->addWidget(m_mpv);
    setLayout(vl);

    setFocusPolicy(Qt::NoFocus);
    m_mpv->setFocusPolicy(Qt::NoFocus);

    connect(m_mpv, &MpvWidget::durationChanged,  this, &VideoPlayer::durationChanged);
    connect(m_mpv, &MpvWidget::positionChanged,  this, &VideoPlayer::positionChanged);
    connect(m_mpv, &MpvWidget::videoPaused,      this, &VideoPlayer::videoPaused);
    connect(m_mpv, &MpvWidget::playbackFinished, this, &VideoPlayer::playbackFinished);
}

bool VideoPlayerMpv::showVideo(const QString &file) {
    if(file.isEmpty())
        return false;
    // 直接走 argv 命令，避免 QVariant->mpv_node 序列化
    const QByteArray path = file.toUtf8();
    const char *args[] = {"loadfile", path.constData(), nullptr};
    m_mpv->command(args);
    setPaused(false);
    return true;
}

void VideoPlayerMpv::seek(int pos) {
    const std::string s = std::to_string(pos);
    const char *args[] = {"seek", s.c_str(), "absolute", nullptr};
    m_mpv->command(args);
}

void VideoPlayerMpv::seekRelative(int pos) {
    const std::string s = std::to_string(pos);
    const char *args[] = {"seek", s.c_str(), "relative", nullptr};
    m_mpv->command(args);
}

void VideoPlayerMpv::pauseResume() {
    const char *args[] = {"cycle", "pause", nullptr};
    m_mpv->command(args);
}

void VideoPlayerMpv::frameStep() {
    m_mpv->command("frame-step");
}

void VideoPlayerMpv::frameStepBack() {
    m_mpv->command("frame-back-step");
}

void VideoPlayerMpv::stop() {
    m_mpv->command("stop");
}

void VideoPlayerMpv::setPaused(bool mode) {
    m_mpv->setProperty("pause", mode);
}

void VideoPlayerMpv::setMuted(bool mode) {
    m_mpv->setMuted(mode);
}

bool VideoPlayerMpv::muted() const {
    return m_mpv->muted();
}

void VideoPlayerMpv::volumeUp() {
    m_mpv->setVolume(m_mpv->volume() + 5);
}

void VideoPlayerMpv::volumeDown() {
    m_mpv->setVolume(m_mpv->volume() - 5);
}

void VideoPlayerMpv::setVolume(int vol) {
    m_mpv->setVolume(vol);
}

int VideoPlayerMpv::volume() const {
    return m_mpv->volume();
}

void VideoPlayerMpv::setVideoUnscaled(bool mode) {
    if(mode)
        m_mpv->setOption("video-unscaled", "downscale-big");
    else
        m_mpv->setOption("video-unscaled", "no");
}

void VideoPlayerMpv::mousePressEvent(QMouseEvent *event) {
    // different platforms, double-click the mouse to judge inconsistencies
    QWidget::mousePressEvent(event);
    event->ignore();
}

void VideoPlayerMpv::mouseMoveEvent(QMouseEvent *event) {
    QWidget::mouseMoveEvent(event);
    event->ignore();
}

void VideoPlayerMpv::mouseReleaseEvent(QMouseEvent *event) {
    QWidget::mouseReleaseEvent(event);
    event->ignore();
}

void VideoPlayerMpv::setLoopPlayback(bool mode) {
    m_mpv->setRepeat(mode);
}

VideoPlayer *CreatePlayerWidget() {
    return new VideoPlayerMpv();
}
