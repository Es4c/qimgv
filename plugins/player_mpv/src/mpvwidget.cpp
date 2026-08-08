#include "mpvwidget.h"
#include <stdexcept>
#include <array>

// mpv 线程回调：用 functor 形式入队，避免每次按名字查元方法。
void MpvWidget::wakeup(void *ctx) {
    MpvWidget *self = static_cast<MpvWidget*>(ctx);
    QMetaObject::invokeMethod(self, [self] { self->on_mpv_events(); }, Qt::QueuedConnection);
}

static void *get_proc_address(void *ctx, const char *name) noexcept {
    Q_UNUSED(ctx);
    QOpenGLContext *glctx = QOpenGLContext::currentContext();
    if (!glctx)
        return nullptr;
    return reinterpret_cast<void *>(glctx->getProcAddress(QByteArray(name)));
}

MpvWidget::MpvWidget(QWidget *parent, Qt::WindowFlags f)
    : QOpenGLWidget(parent, f), m_volume(100)
{
    mpv = mpv_create();
    if(!mpv)
        throw std::runtime_error("could not create mpv context");

    this->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    //mpv_set_option_string(mpv, "terminal", "yes");
    //mpv_set_option_string(mpv, "msg-level", "all=v");
    mpv_set_option_string(mpv, "vo", "libmpv");

    if (mpv_initialize(mpv) < 0)
        throw std::runtime_error("could not initialize mpv context");

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
}

MpvWidget::~MpvWidget() {
    makeCurrent();
    if (mpv_gl)
        mpv_render_context_free(mpv_gl);
    mpv_terminate_destroy(mpv);
}

void MpvWidget::command(const QVariant& params) {
    mpv::qt::command(mpv, params);
}

void MpvWidget::command(const char *cmd) {
    mpv_command_string(mpv, cmd);
}

void MpvWidget::command(const char *const args[]) {
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

void MpvWidget::initializeGL() {
    mpv_opengl_init_params gl_init_params{get_proc_address, nullptr};
    std::array<mpv_render_param, 3> params{
        mpv_render_param{MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
        mpv_render_param{MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init_params},
        mpv_render_param{MPV_RENDER_PARAM_INVALID, nullptr}
    };

    if(mpv_render_context_create(&mpv_gl, mpv, params.data()) < 0)
        throw std::runtime_error("failed to initialize mpv GL context");
    mpv_render_context_set_update_callback(mpv_gl, MpvWidget::on_update, reinterpret_cast<void *>(this));
}

void MpvWidget::paintGL() {
    static mpv_opengl_fbo mpfbo{0, 0, 0, 0};
    static constexpr int flip_y{1};
    static std::array<mpv_render_param, 3> params{
        mpv_render_param{MPV_RENDER_PARAM_OPENGL_FBO, &mpfbo},
        mpv_render_param{MPV_RENDER_PARAM_FLIP_Y, &flip_y},
        mpv_render_param{MPV_RENDER_PARAM_INVALID, nullptr}
    };
    mpfbo = mpv_opengl_fbo{static_cast<int>(defaultFramebufferObject()), width(), height(), 0};
    // See render_gl.h on what OpenGL environment mpv expects, and
    // other API details.
    mpv_render_context_render(mpv_gl, params.data());
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

// Make Qt invoke mpv_render_context_render() to draw a new/updated video frame.
void MpvWidget::maybeUpdate() {
    // If the Qt window is not visible, Qt's update() will just skip rendering.
    // This confuses mpv's render API, and may lead to small occasional
    // freezes due to video rendering timing out.
    // Handle this by manually redrawing.
    // Note: Qt doesn't seem to provide a way to query whether update() will
    //       be skipped, and the following code still fails when e.g. switching
    //       to a different workspace with a reparenting window manager.
    if(window()->isMinimized()) {
        makeCurrent();
        paintGL();
        context()->swapBuffers(context()->surface());
        doneCurrent();
    } else {
        update();
    }
}

void MpvWidget::on_update(void *ctx) {
    // 每帧渲染回调：functor 入队，避免按名字查元方法
    MpvWidget *self = static_cast<MpvWidget*>(ctx);
    QMetaObject::invokeMethod(self, [self] { self->maybeUpdate(); });
}

void MpvWidget::showEvent(QShowEvent *event) {
    QOpenGLWidget::showEvent(event);
    updateDevicePixelRatio();
}

void MpvWidget::resizeEvent(QResizeEvent *event) {
    QOpenGLWidget::resizeEvent(event);
    updateDevicePixelRatio();
}

void MpvWidget::changeEvent(QEvent *event) {
    QOpenGLWidget::changeEvent(event);
    // 跨屏移动时高 DPI 比例会变化，需刷新缓存
    if (event->type() == QEvent::ScreenChangeInternal)
        updateDevicePixelRatio();
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
