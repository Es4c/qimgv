#pragma once

#include <QtCore/QMetaObject>
#include <QOpenGLWidget>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>

#include <mpv/client.h>
#include <mpv/render_gl.h>
#include "qthelper.hpp"

#include <QDebug>
#include <ctime>
#include <QSurfaceFormat>
#include <QTimer>

class MpvWidget final : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit MpvWidget(QWidget *parent = nullptr, Qt::WindowFlags f = Qt::Widget);
    ~MpvWidget() override;

    void command(const QVariant& params);
    void command(const char *cmd);
    void command(const char *const args[]);
    void setOption(const QString &name, const QVariant &value);
    void setProperty(const QString& name, const QVariant& value);
    [[nodiscard]] QVariant getProperty(const QString& name) const;
    
    // Related to this:
    // https://github.com/gnome-mpv/gnome-mpv/issues/245
    // Let's hope this wont break more than it fixes
    [[nodiscard]] int width() const {
        return static_cast<int>(QOpenGLWidget::width() * m_devicePixelRatio);
    }
    [[nodiscard]] int height() const {
        return static_cast<int>(QOpenGLWidget::height() * m_devicePixelRatio);
    }
    
    void setMuted(bool mode);
    void setRepeat(bool mode);
    [[nodiscard]] bool muted() const;
    [[nodiscard]] int volume() const;
    void setVolume(int vol);

signals:
    void durationChanged(int value);
    void positionChanged(int value);
    void videoPaused(bool);
    void playbackFinished();

protected:
    void initializeGL() override;
    void paintGL() override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;

private slots:
    void on_mpv_events();
    void maybeUpdate();

 private:
    // 缓存设备像素比，避免 paintGL 每帧调用 devicePixelRatioF()。
    void updateDevicePixelRatio() { m_devicePixelRatio = devicePixelRatioF(); }
    void handle_mpv_event(mpv_event *event);
    static void wakeup(void *ctx);
    static void on_update(void *ctx);

    mpv_handle *mpv;
    mpv_render_context *mpv_gl;
    int m_volume;
    qreal m_devicePixelRatio = 1.0;
};
