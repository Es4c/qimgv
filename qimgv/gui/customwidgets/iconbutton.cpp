#include "iconbutton.h"

IconButton::IconButton(QWidget *parent)
    : IconWidget(parent)
{
}

void IconButton::setCheckable(bool mode) {
    mCheckable = mode;
}

bool IconButton::isChecked() {
    return mChecked;
}

void IconButton::setChecked(bool mode) {
    if(mCheckable && mode != mChecked) {
        mPressed = false;
        setProperty("pressed", false);
        mChecked = mode;
        setProperty("checked", mChecked);
        style()->unpolish(this);
        style()->polish(this);
    }
}

void IconButton::mousePressEvent(QMouseEvent *event) {
    Q_UNUSED(event)
    if(mCheckable) {
        setChecked(!mChecked); // setChecked 内部已处理 unpolish/polish
        emit toggled(mChecked);
        return;
    }
    if(!mPressed) {
        mPressed = true;
        setProperty("pressed", true);
        style()->unpolish(this);
        style()->polish(this);
    }
}

void IconButton::mouseReleaseEvent(QMouseEvent *event) {
    Q_UNUSED(event)
    const bool wasPressed = mPressed;
    mPressed = false;
    if(rect().contains(event->position().toPoint()) && !mCheckable) {
        emit clicked();
    }
    if(!mChecked && wasPressed) {
        setProperty("pressed", false);
        style()->unpolish(this);
        style()->polish(this);
    }
}

void IconButton::mouseMoveEvent(QMouseEvent *event) {
    if(mChecked || !mPressed)
        return;
    if(rect().contains(event->position().toPoint())) {
        if(!property("pressed").toBool()) {
            setProperty("pressed", true);
            style()->unpolish(this);
            style()->polish(this);
        }
    } else {
        if(property("pressed").toBool()) {
            setProperty("pressed", false);
            style()->unpolish(this);
            style()->polish(this);
        }
    }
}
