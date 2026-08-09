#include "mpvwidget.h"
#include <stdexcept>

// mpv 线程回调：用 functor 形式入队，避免每次按名字查元方法。
void MpvWidget::wakeup(void *ctx) {
    MpvWidget *self = static_cast<MpvWidget*>(ctx);
    QMetaObject::invokeMethod(self, [self] { self->on_mpv_events(); }, Qt::QueuedConnection);
}

MpvWidget::MpvWidget(QWidget *parent, Qt::WindowFlags f)
    : QWidget(parent, f), m_volume(100)
{
    mpv = mpv_create();
    if(!mpv)
        throw std::runtime_error("could not create mpv context");

    int64_t wid = parent->winId();
    mpv_set_option(mpv, "wid", MPV_FORMAT_INT64, &wid);
    
    this->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    mpv_set_option_string(mpv, "input-cursor", "no");   // no mouse handling
    mpv_set_option_string(mpv, "cursor-autohide", "no");// no cursor-autohide, we handle that
    //mpv_set_option_string(mpv, "terminal", "yes");
    //mpv_set_option_string(mpv, "msg-level", "all=v");

    // Request hw decoding, just for testing.
    mpv::qt::set_property(mpv, "hwdec", "auto");

    //mpv::qt::set_property(mpv, "video-unscaled", "downscale-big");

    // Loop video
    setRepeat(true);

    // Unmute
    setMuted(false);
    
    // reply_userdata 用于事件分发时直接区分属性，避免每次 strcmp
    mpv_observe_property(mpv, 1, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv, 2, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv, 3, "pause", MPV_FORMAT_FLAG);
    mpv_set_wakeup_callback(mpv, wakeup, this);
    
    if (mpv_initialize(mpv) < 0)
        throw std::runtime_error("could not initialize mpv context");    
}

MpvWidget::~MpvWidget() {
    mpv_terminate_destroy(mpv);
}

void MpvWidget::command(const QVariant& params) {
    mpv::qt::command(mpv, params);
}

void MpvWidget::command(const char *cmd) {
    mpv_command_string(mpv, cmd);
}

void MpvWidget::command(const char **args) {
    mpv_command(mpv, args);
}

void MpvWidget::setProperty(const QString& name, const QVariant& value) {
    mpv::qt::set_property(mpv, name, value);
}

QVariant MpvWidget::getProperty(const QString &name) const {
    return mpv::qt::get_property(mpv, name);
}

void MpvWidget::setOption(const QString& name, const QVariant& value) {
    mpv::qt::set_property(mpv, name, value);
}

void MpvWidget::on_mpv_events() {
    // Process all events, until the event queue is empty.
    while (mpv) {
        mpv_event *event = mpv_wait_event(mpv, 0);
        if (event->event_id == MPV_EVENT_NONE) {
            break;
        }
        handle_mpv_event(event);
    }
}

void MpvWidget::handle_mpv_event(mpv_event *event) {
    if (event->event_id != MPV_EVENT_PROPERTY_CHANGE)
        return;
    // 按 reply_userdata 直接分发，避免逐属性 strcmp
    mpv_event_property *prop = reinterpret_cast<mpv_event_property*>(event->data);
    switch (event->reply_userdata) {
    case 2: // time-pos
        if (prop->format == MPV_FORMAT_DOUBLE)
            emit positionChanged(static_cast<int>(*reinterpret_cast<double*>(prop->data)));
        break;
    case 1: // duration
        if (prop->format == MPV_FORMAT_DOUBLE)
            emit durationChanged(static_cast<int>(*reinterpret_cast<double*>(prop->data)));
        else if (prop->format == MPV_FORMAT_NONE)
            emit playbackFinished();
        break;
    case 3: // pause
        if (prop->format == MPV_FORMAT_FLAG)
            emit videoPaused(*reinterpret_cast<int*>(prop->data) == 1);
        break;
    default:
        break; // 忽略不感兴趣的事件
    }
}

void MpvWidget::setMuted(bool mode) {
    if(mode)
        mpv::qt::set_property(mpv, "mute", "yes");
    else
        mpv::qt::set_property(mpv, "mute", "no");
}

bool MpvWidget::muted() const {
    return mpv::qt::get_property_flag(mpv, "mute");
}

int MpvWidget::volume() const {
    return m_volume;
}

void MpvWidget::setVolume(int vol) {
    vol = qBound(0, vol, 100);
    m_volume = vol;
    mpv::qt::set_property(mpv, "volume", vol);
}

void MpvWidget::setRepeat(bool mode) {
    if(mode)
        mpv::qt::set_property(mpv, "loop-file", "inf");
    else
        mpv::qt::set_property(mpv, "loop-file", "no");
}
