// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include <set>

#include <QLabel>
#include <QFrame>
#include <QCheckBox>
#include <QRadioButton>
#include <QPushButton>
#include <QProxyStyle>
#include <QSlider>
#include <QLineEdit>
#include <QSpacerItem>
#include <QIntValidator>
#include <QComboBox>
#include <QFile>
#include <QTextStream>
#include <QTabWidget>
#include <QWheelEvent>

#include "nlohmann/json.hpp"
#include "misc.h"
#include "commonui.h"
#include "filtermodelhelpers.h"
#include "marqueebutton.h"
#include "marqueelabel.h"
#include "misc.h"
#include "../modelapp.h"
#include "../obsinfo.h"
#include "../qstringconv.h"

namespace {

// Keep Qt's native slider positioning logic, but make left-clicks on the
// groove jump directly to the clicked position instead of moving by pageStep.
class CiciSliderStyle final : public QProxyStyle {
public:
    int styleHint(StyleHint hint,
                  const QStyleOption* option = nullptr,
                  const QWidget* widget = nullptr,
                  QStyleHintReturn* returnData = nullptr) const override {
        const int value =
            QProxyStyle::styleHint(hint, option, widget, returnData);
        if (hint == QStyle::SH_Slider_AbsoluteSetButtons) {
            return value | static_cast<int>(Qt::LeftButton);
        }
        return value;
    }
};

class CiciSlider final : public QSlider {
public:
    explicit CiciSlider(Qt::Orientation orientation, QWidget* parent = nullptr)
        : QSlider(orientation, parent) {
        auto* sliderStyle = new CiciSliderStyle;
        // QWidget::setStyle() does not take ownership of the style object.
        sliderStyle->setParent(this);
        setStyle(sliderStyle);
    }

protected:
    void wheelEvent(QWheelEvent* event) override {
        // Ignoring the event lets the containing scroll area handle it.
        event->ignore();
    }
};

void applySliderStyle(QSlider* slider) {
    slider->setStyleSheet(QStringLiteral(
        "QSlider {"
        "  min-height: 18px;"
        "}"
        "QSlider::groove:horizontal {"
        "  height: 4px;"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QSlider::sub-page:horizontal {"
        "  background: #4f6fff;"
        "  border-radius: 2px;"
        "}"
        "QSlider::add-page:horizontal {"
        "  background: rgba(128, 136, 150, 120);"
        "  border-radius: 2px;"
        "}"
        "QSlider::sub-page:horizontal:disabled {"
        "  background: rgba(154, 160, 172, 120);"
        "}"
        "QSlider::add-page:horizontal:disabled {"
        "  background: rgba(112, 118, 130, 90);"
        "}"
    ));
}

}

class VideoInputLabel : public QLabel, public IObsEventCallback, public IFilterModelChangeObserver {
public:
	VideoInputLabel(GlobalModel *app, const QString &fallback)
        : QLabel(videoInputDisplayText(app, fallback))
        , app_(app)
        , fallback_(fallback) {
        setMinimumSize(sizeHint());
        setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

        assert(app_ != nullptr);
        app_->registerFilterModelChangeObserver(this);
        auto* info = currentObsInfo(app_);
	    if (info != nullptr) {
		    info->registerObserver(this);
	    }
    }

    ~VideoInputLabel() {
	    auto *info = currentObsInfo(app_);
	    if (info != nullptr) {
		    info->unregisterObserver(this);
	    }
        app_->unregisterFilterModelChangeObserver(this);
    }

private:
    void onFilterModelChanged(FilterModel* oldModel, FilterModel* newModel) override {
        if (oldModel != nullptr) {
            oldModel->obsInfo()->unregisterObserver(this);
        }
        if (newModel != nullptr) {
            newModel->obsInfo()->registerObserver(this);
        }

        setText(videoInputDisplayText(app_, fallback_));
        setMinimumSize(sizeHint());
    }

    void onVideoInputChanged(const ObsUtility::VideoInputInfo& info) override {
        setText(info.displayName_.empty() ? fallback_ : from_u8(info.displayName_));
        setMinimumSize(sizeHint());
    }

private:
    GlobalModel *app_ = nullptr;
    QString fallback_;
};

QLabel* createLabel(const QString& str) {
    QLabel* label = new QLabel(str);
    label->setMinimumSize(label->sizeHint());
    label->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    
    return label;
}

QLabel* createMarqueeLabel(const QString& str) {
    MarqueeLabel* label = new MarqueeLabel(str);
    label->setMinimumSize(label->sizeHint());
    label->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

    return label;
}

QLabel* createVideoInputLabel(GlobalModel* app, const QString& fallback) {
    return new VideoInputLabel(app, fallback);
}

QLabel* createFixWidthLabel(const QString& str, int w/* = 80*/) {
    QLabel* label = new QLabel(str);
    label->setFixedWidth(w);
    label->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    
    return label;
}

QLabel* createFixWidthMarqueeLabel(const QString& str, int w/* = 80*/) {
    MarqueeLabel* label = new MarqueeLabel(str);
    label->setFixedWidth(w);
    label->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

    return label;
}

QCheckBox* createCheckBox(const QString& str) {
    QCheckBox* ck = new QCheckBox(str);
    ck->setMinimumSize(ck->sizeHint());
    ck->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    
    return ck;
}

QRadioButton* createRadioButton(const QString& str) {
    QRadioButton* btn = new QRadioButton(str);
    btn->setMinimumSize(btn->sizeHint());
    btn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    
    return btn;
}

QPushButton* createPushButton(const QString& str) {
    QPushButton* btn = new QPushButton(str);
    btn->setMinimumSize(btn->sizeHint());
    btn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    
    return btn;
}

QPushButton* createMarqueeButton(const QString& str) {
    MarqueeButton* btn = new MarqueeButton(str);
    btn->setMinimumSize(btn->sizeHint());
    btn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

    return btn;
}

QSlider* createSlider() {
    QSlider* slider = new CiciSlider(Qt::Horizontal);
    slider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    slider->setRange(0, 100);
    applySliderStyle(slider);
    
    return slider;
}

QSlider* createFixWidthSlider(int w/* = 300*/) {
    QSlider* slider = new CiciSlider(Qt::Horizontal);
    slider->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    slider->setRange(0, 100);
    slider->setFixedWidth(w);
    applySliderStyle(slider);
    
    return slider;
}

QLineEdit* createEdit() {
    QLineEdit* edit = new QLineEdit;
    edit->setValidator(new QIntValidator(0, 100, edit));
    edit->setFixedSize(36, 24);
    
    return edit;
}

QComboBox* createComboBox() {
    QComboBox* box = new QComboBox;
    box->setMinimumSize(box->sizeHint());
    box->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    
    return box;
}

void applyDisabledTabStyle(QTabWidget* tabWidget) {
    assert(tabWidget != nullptr);
    tabWidget->setStyleSheet(QStringLiteral(
        "QTabBar::tab:disabled { color: #737985; }"
        "QTabBar::tab:selected:disabled { color: #737985; background-color: #2b303a; }"));
}

QLayoutItem* createFixedHorizontalSpacer(int width) {
    return new QSpacerItem(width, 20, QSizePolicy::Fixed, QSizePolicy::Fixed);
}

QLayoutItem* createExpandingHorizontalSpacer() {
    return new QSpacerItem(20, 20, QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QLayoutItem* createExpandingVerticalSpacer() {
    return new QSpacerItem(20, 20, QSizePolicy::Fixed, QSizePolicy::Expanding);
}

QWidget* createHSeperator() {
    QFrame *hSeparator = new QFrame;
    hSeparator->setFrameShape(QFrame::HLine); // Horizontal separator.
    hSeparator->setFrameShadow(QFrame::Sunken);
    
    return hSeparator;
}

QWidget* createVSeperator() {
    QFrame *vSeparator = new QFrame;
    vSeparator->setFrameShape(QFrame::VLine); // Vertical separator.
    vSeparator->setFrameShadow(QFrame::Sunken);
    
    return vSeparator;
}

const QIcon& ciciPluginIcon() {
    static const QIcon icon(from_path(FileSystemOp::resourcesPath() / u8"cicilogo.png"));
    return icon;
}

void applyCiciWindowIcon(QWidget* widget) {
    if (widget != nullptr) {
        widget->setWindowIcon(ciciPluginIcon());
    }
}

uint32_t uniqueId(const std::vector<uint32_t>& ids) {
    std::set<uint32_t> idSet(ids.begin(), ids.end());
    uint32_t id = 1;
    while (idSet.find(id) != idSet.end()) {
        ++id;
    }

    return id;
}

QString videoInputDisplayText(GlobalModel *app, const QString &fallback) {
    if (auto* info = currentObsInfo(app); info != nullptr) {
        std::u8string name = info->videoInputDisplayName();
        if (!name.empty()) {
            return from_u8(name);
        }
    }

    return fallback;
}
