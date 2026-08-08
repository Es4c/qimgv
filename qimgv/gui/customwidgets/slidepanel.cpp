#include "slidepanel.h"

SlidePanel::SlidePanel(FloatingWidgetContainer *parent)
    : FloatingWidget(parent),
      mPosition(PANEL_TOP)
{
    mLayout = new QHBoxLayout();
    mLayout->setSpacing(0);
    mLayout->setContentsMargins(0, 0, 0, 0);
    this->setLayout(mLayout);
    this->setAttribute(Qt::WA_NoMousePropagation, true);
    this->setFocusPolicy(Qt::NoFocus);
    mLayout->setDirection(QBoxLayout::LeftToRight);
    QWidget::hide();
}

SlidePanel::~SlidePanel() = default;

void SlidePanel::hide() {
    QWidget::hide();
}

void SlidePanel::show() {
    QWidget::show();
}

void SlidePanel::setPosition(PanelPosition p) {
    mPosition = p;
}

PanelPosition SlidePanel::position() {
    return mPosition;
}

QRect SlidePanel::triggerRect() {
    return mTriggerRect;
}

bool SlidePanel::layoutManaged() {
    return mLayoutManaged;
}

void SlidePanel::setLayoutManaged(bool mode) {
    mLayoutManaged = mode;
}

void SlidePanel::recalculateGeometry() {
}
