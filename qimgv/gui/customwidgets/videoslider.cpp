#include "videoslider.h"

VideoSlider::VideoSlider(QWidget *parent) : QSlider(parent) {
}

void VideoSlider::mousePressEvent(QMouseEvent *event) {
    if(event->button() == Qt::LeftButton) {
        event->accept();
        setValueAtCursor(event->position().toPoint());
    }
}

void VideoSlider::mouseMoveEvent(QMouseEvent *event) {
    if(event->buttons() & Qt::LeftButton) {
        event->accept();
        setValueAtCursor(event->position().toPoint());
    }
}

void VideoSlider::setValueAtCursor(QPoint pos) {
    const int newValue = (orientation() == Qt::Vertical)
        ? minimum() + ((maximum() - minimum()) * (height() - pos.y())) / height()
        : minimum() + ((maximum() - minimum()) * pos.x()) / width();
    if(newValue != value()) {
        setValue(newValue);
        emit sliderMovedX(newValue);
    }
}
