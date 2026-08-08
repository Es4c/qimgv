#include "mainpanel.h"

MainPanel::MainPanel(FloatingWidgetContainer *parent) : SlidePanel(parent) {
// buttons stuff
buttonsWidget.setAccessibleName("panelButtonsWidget");
openButton       = new ActionButton("open", ":res/icons/common/buttons/panel/open20.png", 30, this);
openButton->setAccessibleName("ButtonSmall");
openButton->setTriggerMode(TriggerMode::PressTrigger);
settingsButton   = new ActionButton("openSettings", ":res/icons/common/buttons/panel/settings20.png", 30, this);
settingsButton->setAccessibleName("ButtonSmall");
settingsButton->setTriggerMode(TriggerMode::PressTrigger);
exitButton       = new ActionButton("exit", ":res/icons/common/buttons/panel/close16.png", 30, this);
exitButton->setAccessibleName("ButtonSmall");
exitButton->setTriggerMode(TriggerMode::PressTrigger);
pinButton = new ActionButton("", ":res/icons/common/buttons/panel/pin-panel20.png", 30, this);
pinButton->setAccessibleName("ButtonSmall");
pinButton->setTriggerMode(TriggerMode::PressTrigger);
pinButton->setCheckable(true);
connect(pinButton, &ActionButton::toggled, this, &MainPanel::onPinClicked);
buttonsLayout = new QVBoxLayout();
buttonsLayout->setDirection(QBoxLayout::BottomToTop);
buttonsLayout->setSpacing(0);
buttonsLayout->addWidget(settingsButton);
buttonsLayout->addWidget(openButton);
buttonsLayout->addStretch(0);
buttonsLayout->addWidget(pinButton);
buttonsLayout->addWidget(exitButton);
buttonsWidget.setLayout(buttonsLayout);
layout()->addWidget(&buttonsWidget);
// 注意：不在构造函数中调用 readSettings()，避免虚函数调用问题
// readSettings();
//connect(settings, SIGNAL(settingsChanged()), this, SLOT(readSettings()));
}

MainPanel::~MainPanel() = default;

void MainPanel::onPinClicked(bool checked) {
    settings->setPanelPinned(checked);
    emit pinned(checked);
}

void MainPanel::setPosition(PanelPosition p) {
SlidePanel::setPosition(p);
switch(p) {
case PANEL_TOP:
buttonsLayout->setDirection(QBoxLayout::BottomToTop);
layout()->setContentsMargins(0,0,0,1);
buttonsLayout->setContentsMargins(4,0,0,0);
break;
case PANEL_BOTTOM:
buttonsLayout->setDirection(QBoxLayout::BottomToTop);
layout()->setContentsMargins(0,3,0,0);
buttonsLayout->setContentsMargins(4,0,0,0);
break;
case PANEL_LEFT:
buttonsLayout->setDirection(QBoxLayout::LeftToRight);
layout()->setContentsMargins(0,0,1,0);
buttonsLayout->setContentsMargins(0,0,0,4);
break;
case PANEL_RIGHT:
buttonsLayout->setDirection(QBoxLayout::LeftToRight);
layout()->setContentsMargins(1,0,0,0);
buttonsLayout->setContentsMargins(0,0,0,4);
break;
}
recalculateGeometry();
}

void MainPanel::setExitButtonEnabled(bool mode) {
exitButton->setHidden(!mode);
}

QSize MainPanel::sizeHint() const {
    // 按钮列/行的固有尺寸 + 面板自身布局边距，供布局在未固定尺寸时使用
    return layout() ? layout()->sizeHint() : buttonsWidget.sizeHint();
}

void MainPanel::readSettings() {
    auto newPos = settings->panelPosition();
    setPosition(newPos); // 先设置布局方向，sizeHint 才会对应正确的固定尺寸
    const QSize sh = sizeHint();
    if(newPos == PANEL_TOP || newPos == PANEL_BOTTOM) {
        this->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFixedHeight(sh.height());
        setFixedWidth(QWIDGETSIZE_MAX);
    } else {
        this->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        setFixedWidth(sh.width());
        setFixedHeight(QWIDGETSIZE_MAX);
    }
    pinButton->setChecked(settings->panelPinned());
}

