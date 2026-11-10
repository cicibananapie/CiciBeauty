// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include <assert.h>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QSlider>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTabWidget>
#include <QStackedWidget>
#include <QInputDialog>
#include <QSignalBlocker>

#include "log.h"
#include "nlohmann/json.hpp"
#include "commonui.h"
#include "filtermodelhelpers.h"
#include "legwidget.h"
#include "bodyshapingsettingcontroller.h"
#include "solutiontabhelper.h"
#include "../qstringconv.h"
#include "../modelapp.h"
#include "jsonutility.h"
#include "../statistics/settingspageanalytics.h"

namespace {
constexpr int KLeftMargin = 20;
constexpr int kManualLegSliderMin = 0;
constexpr int kManualLegSliderNeutral = 50;
constexpr int kManualLegSliderMax = 100;
constexpr uint32_t kDefaultUiValue = 50;
constexpr float kManualLegMinLinesDistance = 0.02f;
constexpr float kManualLegAmplitude = 0.0836f;
constexpr float kManualShortLegMaxAmplitudeRatio = 0.6f;
constexpr bool kManualLegDebugLog = true;

using JsonUtility::arrayAt;
using JsonUtility::boolAt;
using JsonUtility::objectAt;
using JsonUtility::u8StringAt;
using JsonUtility::uintAt;

const char* manualLegDirectionName(int direction) {
    switch (direction) {
        case 0:
            return "up";
        case 1:
            return "down";
        case 2:
            return "bidirection";
        default:
            return "unknown";
    }
}

float clampManualLegFloat(float value, float minValue, float maxValue) {
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    
    return value;
}

float sliderValueToIntensity(int value) {
    return std::abs(value - kManualLegSliderNeutral) * 1.0f / kManualLegSliderNeutral;
}

int clampManualLegSlider(int value) {
    return std::max(kManualLegSliderMin, std::min(kManualLegSliderMax, value));
}

std::vector<LegZoneEditorWidget::OriginalZoneInfo> defaultManualLegZones() {
    std::vector<LegZoneEditorWidget::OriginalZoneInfo> zones;
    const float step = 1.0f / 6.0f;
    for (int i = 0; i < 3; i++) {
        LegZoneEditorWidget::OriginalZoneInfo zone;
        zone.id = i;
        zone.top = (i + 2) * step;
        zone.bottom = (i + 3) * step;
        zones.push_back(zone);
    }
    
    return zones;
}

nlohmann::json bodySolutionSkeleton(uint32_t id, const QString& name = {}) {
    auto manual = nlohmann::json::object();
    manual["version"] = 1;
    manual["selectedZoneId"] = -1;
    manual["previewOffset"] = 0;

    auto zones = nlohmann::json::array();
    const double step = 1.0 / 6.0;
    for (int i = 0; i < 3; ++i) {
        auto zone = nlohmann::json::object();
        zone["id"] = i;
        zone["top"] = (i + 2) * step;
        zone["bottom"] = (i + 3) * step;
        zone["sliders"] = {50, 50, 50};
        zone["direction"] = 1;
        zones.push_back(zone);
    }
    manual["zones"].swap(zones);

    auto j = nlohmann::json::object();
    j["id"] = id;
    j["customName"] = to_json(name);
    j["slimBody"] = kDefaultUiValue;
    j["smallHead"] = kDefaultUiValue;
    j["selected"] = 0;
    j["longLeg"] = kDefaultUiValue;
    j["manual"] = std::move(manual);
    return j;
}

}

static QString zoneName(int i) {
    QString str;
    switch (i) {
        case 1:
            str = QObject::tr("Zone 1");
            break;
        case 2:
            str = QObject::tr("Zone 2");
            break;
        case 3:
            str = QObject::tr("Zone 3");
            break;
        case 4:
            str = QObject::tr("Zone 4");
            break;
        default:
            assert(false);
            break;
    }
    
    return str;
}

void ManualLengthenLegController::CtrlGroup::setEnabled(bool enabled) {
    zoneLabel_->setEnabled(enabled);
    directionLabel_->setEnabled(enabled);
    upRadio_->setEnabled(enabled);
    downRadio_->setEnabled(enabled);
    biDirectionRadio_->setEnabled(enabled);
    shortenLabel_->setEnabled(enabled);
    slider_->setEnabled(enabled);
    lengthenLabel_->setEnabled(enabled);
}

int ManualLengthenLegController::nextSolutionId_ = 0;

ManualLengthenLegController::ManualLengthenLegController(GlobalModel* app, QWidget* parent/* = nullptr*/)
: CiciWidget(parent), solutionId_(generateSolutionId()), app_(app) {
    setWindowTitle(tr("Edit zones"));
    applyCiciWindowIcon(this);
    if (kManualLegDebugLog) {
        LogInfo << "[ManualLeg] create solutionId=" << solutionId_ << logEnd;
    }
    
    QPushButton* resetBtn = createPushButton(tr("Reset zones"));
    QPushButton* addBtn = createPushButton(tr("Add zone"));
    removeBtn_ = createPushButton(tr("Delete zone"));
    
    QHBoxLayout* toolbar = new QHBoxLayout;
    toolbar->setSpacing(KSpacing);
    toolbar->addStretch();
    toolbar->addWidget(resetBtn);
    toolbar->addWidget(addBtn);
    toolbar->addWidget(removeBtn_);
    toolbar->addStretch();
    
    zoneEditor_ = new LegZoneEditorWidget(app_);
    previewWidget_ = new LegPreviewWidget(app_);
    zoneEditor_->setMinimumSize(320, 360);
    previewWidget_->setMinimumSize(320, 360);
    
    QHBoxLayout* views = new QHBoxLayout;
    views->setSpacing(KSpacing);
    views->addWidget(zoneEditor_, 1);
    views->addWidget(previewWidget_, 1);
    
    QVBoxLayout* vbox = new QVBoxLayout;
    vbox->setSpacing(KSpacing / 2);
    vbox->addItem(toolbar);
    vbox->addItem(views);
    createCtrlGroup(vbox);
    setLayout(vbox);
    resize(760, 520);
    
    QObject::connect(resetBtn, &QPushButton::clicked, this, [this]() {
        emit uiActionTriggered(QStringLiteral("zone_reset"));
        onResetBtnClicked();
    });
    QObject::connect(addBtn, &QPushButton::clicked, this, &ManualLengthenLegController::onAddBtnClicked);
    QObject::connect(removeBtn_, &QPushButton::clicked, this, &ManualLengthenLegController::onRemoveBtnClicked);
    QObject::connect(zoneEditor_, &LegZoneEditorWidget::zonesDirty, this, &ManualLengthenLegController::onEditorZonesDirty);
    QObject::connect(zoneEditor_, &LegZoneEditorWidget::zoneSelected, this, &ManualLengthenLegController::onZoneSelected);
    QObject::connect(zoneEditor_, &LegZoneEditorWidget::zoneDeleteRequested, this, &ManualLengthenLegController::onZoneDeleteRequested);
    QObject::connect(previewWidget_, &LegPreviewWidget::previewOffsetChangeRequested,
                     this, &ManualLengthenLegController::onPreviewOffsetRequested);
  
    onResetBtnClicked();
}

ManualLengthenLegController::~ManualLengthenLegController() {
}

void ManualLengthenLegController::json2Me(const nlohmann::json& j) {
    loadingConfig_ = true;
    if (!j.is_object()) {
        onResetBtnClicked();
        loadingConfig_ = false;
        return;
    }
    const bool hasZones = j.contains("zones") && j["zones"].is_array();
    if (!hasZones) {
        onResetBtnClicked();
        loadingConfig_ = false;
        return;
    }
    
    std::vector<ZoneState> zones;
    std::vector<LegZoneEditorWidget::OriginalZoneInfo> editorZones;
    int nextId = 0;
    auto appendZone = [&](const ZoneState& zone) {
        LegZoneEditorWidget::OriginalZoneInfo editorZone;
        editorZone.id = zone.id;
        editorZone.top = zone.top;
        editorZone.bottom = zone.bottom;
        zones.push_back(zone);
        editorZones.push_back(editorZone);
        nextId = std::max(nextId, zone.id + 1);
    };
    
    for (const auto& item: j["zones"]) {
        if (!item.is_object()) {
            continue;
        }
        
        ZoneState zone;
        zone.id = item.value("id", nextId);
        zone.top = item.value("top", 0.0f);
        zone.bottom = item.value("bottom", 0.0f);
        if (item.contains("sliders") && item["sliders"].is_array()) {
            for (int i = 0; i < 3 && i < static_cast<int>(item["sliders"].size()); i++) {
                zone.sliders[i] = clampManualLegSlider(item["sliders"][i].get<int>());
            }
        }
        const int direction = item.value("direction", DownDirection);
        if (direction >= UpDirection && direction <= BiDirection) {
            zone.direction = static_cast<AxialDeformationType>(direction);
        }
        appendZone(zone);
    }
    
    zones_ = zones;
    previewOffset_ = j.value("previewOffset", 0.0f);
    selectedZoneId_ = j.value("selectedZoneId", -1);
    zoneEditor_->setZones(editorZones);
    zoneEditor_->setSelectedZone(selectedZoneId_);
    updateCtrlGroup();
    updatePreview();
    loadingConfig_ = false;
}

nlohmann::json ManualLengthenLegController::me2Json() const {
    auto j = nlohmann::json::object();
    j["version"] = 1;
    j["selectedZoneId"] = selectedZoneId_;
    j["previewOffset"] = previewOffset_;
    
    auto zones = nlohmann::json::array();
    for (const auto& zone: zones_) {
        auto item = nlohmann::json::object();
        item["id"] = zone.id;
        item["top"] = zone.top;
        item["bottom"] = zone.bottom;
        auto sliders = nlohmann::json::array();
        for (int i = 0; i < 3; i++) {
            sliders.push_back(zone.sliders[i]);
        }
        item["sliders"].swap(sliders);
        item["direction"] = static_cast<int>(zone.direction);
        zones.push_back(item);
    }
    j["zones"].swap(zones);
    
    return j;
}

int ManualLengthenLegController::generateSolutionId() {
    return nextSolutionId_++;
}

void ManualLengthenLegController::createCtrlGroup(QVBoxLayout* layout) {
    ctrl_.zoneLabel_ = createLabel(tr("No zone selected"));
    QHBoxLayout* title = new QHBoxLayout;
    title->addWidget(ctrl_.zoneLabel_);
    title->addStretch();
    
    ctrl_.directionLabel_ = createLabel(tr("Direction:"));
    ctrl_.upRadio_ = createRadioButton(tr("Up"));
    ctrl_.downRadio_ = createRadioButton(tr("Down"));
    ctrl_.biDirectionRadio_ = createRadioButton(tr("Bidirection"));
    ctrl_.downRadio_->setChecked(true);
    ctrl_.bg_ = new QButtonGroup(this);
    ctrl_.bg_->addButton(ctrl_.upRadio_, UpDirection);
    ctrl_.bg_->addButton(ctrl_.downRadio_, DownDirection);
    ctrl_.bg_->addButton(ctrl_.biDirectionRadio_, BiDirection);
    
    QHBoxLayout* direction = new QHBoxLayout;
    direction->setContentsMargins(KLeftMargin, 0, 0, 0);
    direction->setSpacing(KSpacing);
    direction->addWidget(ctrl_.directionLabel_);
    direction->addWidget(ctrl_.upRadio_);
    direction->addWidget(ctrl_.downRadio_);
    direction->addWidget(ctrl_.biDirectionRadio_);
    direction->addStretch();
    
    ctrl_.shortenLabel_ = createLabel(tr("Shorten"));
    ctrl_.slider_ = createFixWidthSlider();
    ctrl_.slider_->setRange(kManualLegSliderMin, kManualLegSliderMax);
    ctrl_.slider_->setValue(kManualLegSliderNeutral);
    ctrl_.lengthenLabel_ = createLabel(tr("Lengthen"));
    
    QHBoxLayout* slider = new QHBoxLayout;
    slider->setContentsMargins(KLeftMargin, 0, 0, 0);
    slider->setSpacing(KSpacing);
    slider->addWidget(ctrl_.shortenLabel_);
    slider->addWidget(ctrl_.slider_);
    slider->addWidget(ctrl_.lengthenLabel_);
    slider->addStretch();
    
    layout->addItem(title);
    layout->addItem(direction);
    layout->addItem(slider);
    
    QObject::connect(ctrl_.bg_, &QButtonGroup::idToggled, this, [this](int id, bool checked) {
        ZoneState* zone = selectedZone();
        if (checked && zone != nullptr) {
            zone->direction = static_cast<AxialDeformationType>(id);
            QSignalBlocker blocker(ctrl_.slider_);
            ctrl_.slider_->setValue(zone->currentSlider());
            updatePreview();
            emitStateChanged();
        }
    });
    QObject::connect(ctrl_.bg_, &QButtonGroup::idClicked, this, [this](int id) {
        constexpr std::u8string_view directions[] = {
            u8"direction_up", u8"direction_down", u8"direction_bidirectional",
        };
        if (id < 0 || id >= static_cast<int>(std::size(directions))) {
            return;
        }
        emit uiActionTriggered(from_u8(std::u8string(directions[id])));
    });
    QObject::connect(ctrl_.slider_, &QSlider::valueChanged, this, [this](int value) {
        ZoneState* zone = selectedZone();
        if (zone != nullptr) {
            zone->setCurrentSlider(value);
            updatePreview();
            emitStateChanged();
        }
    });
}

void ManualLengthenLegController::onAddBtnClicked() {
    const int id = zoneEditor_->addZone();
    if (id != -1) {
        zoneEditor_->setSelectedZone(id);
        emit uiActionTriggered(QStringLiteral("zone_add"));
    }
}

void ManualLengthenLegController::onRemoveBtnClicked() {
    assert(selectedZoneId_ != -1);
    onZoneDeleteRequested(selectedZoneId_);
}

void ManualLengthenLegController::onResetBtnClicked() {
    if (kManualLegDebugLog) {
        LogInfo << "[ManualLeg] reset solutionId=" << solutionId_ << logEnd;
    }
    previewOffset_ = 0.0f;
    previewMinOffset_ = 0.0f;
    previewMaxOffset_ = 0.0f;
    selectedZoneId_ = -1;
    {
        QSignalBlocker blocker(zoneEditor_);
        zoneEditor_->setSelectedZone(-1);
        zoneEditor_->setZones(defaultManualLegZones());
    }
    syncZonesFromEditor(false, QStringLiteral("reset"));
}

void ManualLengthenLegController::onEditorZonesDirty() {
    syncZonesFromEditor(true);
}

void ManualLengthenLegController::onZoneSelected(int id) {
    selectedZoneId_ = id;
    previewWidget_->setSelectedZone(id);
    updateCtrlGroup();
}

void ManualLengthenLegController::onZoneDeleteRequested(int id) {
    emit uiActionTriggered(QStringLiteral("zone_remove"));
    zoneEditor_->removeZone(id);
}

void ManualLengthenLegController::onPreviewOffsetRequested(float offset) {
    const float oldOffset = previewOffset_;
    previewOffset_ = clampManualLegFloat(offset, previewMinOffset_, previewMaxOffset_);
    if (kManualLegDebugLog) {
        LogInfo << "[ManualLeg] previewOffsetRequested solutionId=" << solutionId_
                << " requested=" << offset
                << " old=" << oldOffset
                << " clamped=" << previewOffset_
                << " min=" << previewMinOffset_
                << " max=" << previewMaxOffset_ << logEnd;
    }
    updatePreviewOffset();
    emitStateChanged();
}

ManualLengthenLegController::ZoneState* ManualLengthenLegController::findZone(int id) {
    return const_cast<ZoneState*>(static_cast<const ManualLengthenLegController*>(this)->findZone(id));
}

const ManualLengthenLegController::ZoneState* ManualLengthenLegController::findZone(int id) const {
    auto it = std::find_if(zones_.begin(), zones_.end(), [id](const auto& zone) {
        return zone.id == id;
    });
    
    return (it == zones_.end()) ? nullptr : &(*it);
}

ManualLengthenLegController::ZoneState* ManualLengthenLegController::selectedZone() {
    return findZone(selectedZoneId_);
}

void ManualLengthenLegController::syncZonesFromEditor(bool preserveZoneState,
                                                       const QString& uiAction) {
    std::vector<ZoneState> zones;
    const auto &src = zoneEditor_->allZones();
    for (int i = 0; i < src.size(); i++) {
        ZoneState zone;
	    const auto &editorZone = src[i];
        if (preserveZoneState) {
            if (const ZoneState* old = findZone(editorZone.id)) {
                zone = *old;
            }
        }
        zone.id = editorZone.id;
        zone.top = editorZone.top;
        zone.bottom = editorZone.bottom;
        zones.push_back(zone);
    }
    
    zones_.swap(zones);
    if (selectedZoneId_ != -1 && findZone(selectedZoneId_) == nullptr) {
        selectedZoneId_ = -1;
    }

    if (kManualLegDebugLog) {
        LogInfo << "[ManualLeg] syncZonesFromEditor solutionId=" << solutionId_
                << " preserveZoneState=" << preserveZoneState
                << " selectedZoneId=" << selectedZoneId_
                << " zoneCount=" << zones_.size() << logEnd;
        for (int i = 0; i < static_cast<int>(zones_.size()); i++) {
            const ZoneState& zone = zones_[i];
            LogInfo << "[ManualLeg]   zone slot=" << i
                    << " zoneId=" << zone.id
                    << " top=" << zone.top
                    << " bottom=" << zone.bottom
                    << " sliders=[" << zone.sliders[0] << ","
                    << zone.sliders[1] << "," << zone.sliders[2] << "]"
                    << " dir=" << manualLegDirectionName(static_cast<int>(zone.direction))
                    << logEnd;
            if (zone.top < 0.0f || zone.bottom > 1.0f || zone.top >= zone.bottom) {
                LogWarn << "[ManualLeg] invalid zone solutionId=" << solutionId_
                        << " slot=" << i
                        << " zoneId=" << zone.id
                        << " top=" << zone.top
                        << " bottom=" << zone.bottom << logEnd;
            }
        }
    }
    
    updateCtrlGroup();
    updatePreview();
    emitStateChanged(uiAction);
}

void ManualLengthenLegController::updateCtrlGroup() {
    const ZoneState* zone = findZone(selectedZoneId_);
    const int index = zone == nullptr ? -1 : static_cast<int>(zone - zones_.data());
    
    QSignalBlocker sliderBlocker(ctrl_.slider_);
    QSignalBlocker buttonBlocker(ctrl_.bg_);
    ctrl_.zoneLabel_->setText(index == -1 ? tr("No zone selected") : zoneName(index + 1));
    ctrl_.slider_->setValue(zone == nullptr ? kManualLegSliderNeutral : zone->currentSlider());
    ctrl_.downRadio_->setChecked(zone == nullptr || zone->direction == DownDirection);
    ctrl_.upRadio_->setChecked(zone != nullptr && zone->direction == UpDirection);
    ctrl_.biDirectionRadio_->setChecked(zone != nullptr && zone->direction == BiDirection);
    ctrl_.setEnabled(zone != nullptr);
    removeBtn_->setEnabled(zone != nullptr);
}

float ManualLengthenLegController::previewHeightDeltaForZone(const ZoneState& zone) {
    const int slider = zone.currentSlider();
    assert(zone.bottom >= zone.top);
    float longLegDistance = zone.bottom - zone.top;
    if (longLegDistance < kManualLegMinLinesDistance) {
        assert(false);
        return 0.0f;
    }
    
    const float intensity = sliderValueToIntensity(slider);
    if (slider < kManualLegSliderNeutral) {
        const float amplitude = std::min(kManualLegAmplitude,
                                         longLegDistance * kManualShortLegMaxAmplitudeRatio);
        return -intensity * amplitude;
    }
    else if (slider > kManualLegSliderNeutral) {
        return std::min(intensity * kManualLegAmplitude, longLegDistance);
    }
    else {
        return 0.0f;
    }
}

void ManualLengthenLegController::updatePreview() {
    std::vector<LegPreviewWidget::PreviewZoneInfo> previewZones;
    previewZones.reserve(zones_.size());
    
    for (const auto& zone: zones_) {
        LegPreviewWidget::PreviewZoneInfo item;
        item.id = zone.id;
        item.srcTop = zone.top;
        item.srcBottom = zone.bottom;
        item.dstTop = zone.top;
        item.dstBottom = zone.bottom;
        if (zone.direction == UpDirection) {
            item.direction = LegPreviewWidget::StretchUp;
	    }
        else if (zone.direction == DownDirection) {
		    item.direction = LegPreviewWidget::StretchDown;
        }
        else if (zone.direction == BiDirection) {
            item.direction = LegPreviewWidget::StretchBiDirection;
        }
        previewZones.push_back(item);
    }
    
    float contentTop = 0.0f;
    float contentBottom = 1.0f;
    LegPreviewWidget::PreviewZoneInfo *prev = nullptr;
    for (int i = 0; i < static_cast<int>(previewZones.size()); i++) {
        auto& zone = previewZones[i];

        if (prev != nullptr) {
		    float h = zone.srcBottom - zone.srcTop;
		    // Preserve the original gap between adjacent zones.
            const float gap = zones_[i].top - zones_[i - 1].bottom;
		    zone.srcTop = prev->dstBottom + gap;
		    zone.srcBottom = zone.srcTop + h;
		    zone.dstTop = zone.srcTop;
		    zone.dstBottom = zone.dstTop + h;
        }

        const float delta = previewHeightDeltaForZone(zones_[i]);
        float upOffset = 0.0f;
        float downOffset = 0.0f;
        
        if (zone.direction == LegPreviewWidget::StretchUp) {
            if (delta >= 0.0f) {
                upOffset = -delta;
            }
            else {
                downOffset = delta;
            }
        }
        else if (zone.direction == LegPreviewWidget::StretchDown) {
            if (delta >= 0.0f) {
                downOffset = delta;
            }
            else {
                upOffset = -delta;
            }
        }
        else {
            upOffset = -delta / 2.0f;
            downOffset = delta / 2.0f;
        }
        
        zone.dstTop += upOffset;
        zone.dstBottom += downOffset;
        contentTop += upOffset;
        contentBottom += downOffset;
        
        for (int j = 0; j < i; j++) {
            previewZones[j].dstTop += upOffset;
            previewZones[j].dstBottom += upOffset;
        }

        prev = &(previewZones[i]);
    }
    
    const float a = contentTop;
    const float b = contentBottom - 1.0f;
    previewMinOffset_ = std::min(a, b);
    previewMaxOffset_ = std::max(a, b);
    previewOffset_ = clampManualLegFloat(previewOffset_, previewMinOffset_, previewMaxOffset_);
    syncPreviewOffsetToMRE();

    if (kManualLegDebugLog) {
        LogInfo << "[ManualLeg] updatePreview solutionId=" << solutionId_
                << " zoneCount=" << previewZones.size()
                << " offset=" << previewOffset_
                << " minOffset=" << previewMinOffset_
                << " maxOffset=" << previewMaxOffset_ << logEnd;
        for (int i = 0; i < static_cast<int>(previewZones.size()); i++) {
            const auto& zone = previewZones[i];
            LogInfo << "[ManualLeg]   preview slot=" << i
                    << " zoneId=" << zone.id
                    << " src=[" << zone.srcTop << "," << zone.srcBottom << "]"
                    << " dst=[" << zone.dstTop << "," << zone.dstBottom << "]"
                    << " dir=" << static_cast<int>(zone.direction) << logEnd;
        }
    }
    
    previewWidget_->setPreviewOffset(previewOffset_);
    previewWidget_->setZones(previewZones);
    previewWidget_->setSelectedZone(selectedZoneId_);
}

void ManualLengthenLegController::updatePreviewOffset() {
    previewWidget_->setPreviewOffset(previewOffset_);
    syncPreviewOffsetToMRE();
}

void ManualLengthenLegController::syncPreviewOffsetToMRE() {
    // MRE does not expose the final output viewport offset parameter yet.
}

void ManualLengthenLegController::emitStateChanged(const QString& uiAction) {
    if (loadingConfig_) {
        return;
    }

    emit stateChanged(uiAction);
}

BodyShapingSolutionController::BodyShapingSolutionController(GlobalModel* app, QWidget* parent/* = nullptr*/)
: CiciWidget(parent), app_(app) {
    createUI();
}

QString BodyShapingSolutionController::customName() const {
    return name_;
}

QString BodyShapingSolutionController::displayName() const {
    return name_.isEmpty() ? QString("%1 %2").arg(tr("Sln")).arg(id()) : name_;
}

void BodyShapingSolutionController::setCustomName(const QString& customName) {
    name_ = customName.trimmed();
}

void BodyShapingSolutionController::createUI() {
    QLabel* slimBodyLabel = createFixWidthMarqueeLabel(tr("Slim body"));
    slimBodySlider_ = createSlider();
    slimBodyEditor_ = createEdit();
    bindSliderEditor(slimBodySlider_, slimBodyEditor_, "slimBody");
    
    QHBoxLayout* hbox = new QHBoxLayout;
    hbox->setSpacing(KSpacing);
    hbox->addWidget(slimBodyLabel);
    hbox->addWidget(slimBodySlider_);
    hbox->addWidget(slimBodyEditor_);
    
    QLabel* smallHeadLabel = createFixWidthMarqueeLabel(tr("Small head"));
    smallHeadSlider_ = createSlider();
    smallHeadEditor_ = createEdit();
    bindSliderEditor(smallHeadSlider_, smallHeadEditor_, "smallHead");
    
    QHBoxLayout* hbox1 = new QHBoxLayout;
    hbox1->setSpacing(KSpacing);
    hbox1->addWidget(smallHeadLabel);
    hbox1->addWidget(smallHeadSlider_);
    hbox1->addWidget(smallHeadEditor_);
    
    manualLeg_ = createRadioButton(tr("Manual lengthen leg"));
    AILeg_ = createRadioButton(tr("AI lengthen leg"));
    
    QHBoxLayout* hbox2 = new QHBoxLayout;
    hbox2->setSpacing(KSpacing);
    hbox2->addWidget(manualLeg_);
    hbox2->addWidget(AILeg_);
    hbox2->addStretch();
    
    aiPage_ = new QWidget;
    QLabel* longLegLabel = new QLabel(tr("Long leg"));
    longLegSlider_ = createSlider();
    longLegEditor_ = createEdit();
    bindSliderEditor(longLegSlider_, longLegEditor_, "longLeg");
    
    QHBoxLayout* hbox3 = new QHBoxLayout;
    hbox3->setSpacing(KSpacing);
    hbox3->addWidget(longLegLabel);
    hbox3->addWidget(longLegSlider_);
    hbox3->addWidget(longLegEditor_);
    QVBoxLayout* vbox = new QVBoxLayout;
    vbox->addItem(hbox3);
    vbox->addStretch();
    aiPage_->setLayout(vbox);
    
    manualPopup_ = new ManualLengthenLegController(app_);
    QObject::connect(manualPopup_, &ManualLengthenLegController::stateChanged,
                     this, [this](const QString& uiAction) {
                         if (!loadingConfig_) {
                             const auto value = manualPopup_->me2Json();
                             if (uiAction.isEmpty()) {
                                 emit fieldChanged(QStringLiteral("manual"), value);
                             } else {
                                 emit fieldChangedWithAction(QStringLiteral("manual"), value, uiAction);
                             }
                         }
                     });
    QObject::connect(manualPopup_, &ManualLengthenLegController::uiActionTriggered,
                     this, [this](const QString& action) {
                         reportBodyShapingManualLegAction(app_,
                                                          id(),
                                                          to_u8(displayName()),
                                                          to_u8(action));
                     });
    manualPage_ = new QWidget;
    QPushButton* editZonesBtn = createPushButton(tr("Edit zones"));
    QHBoxLayout* hbox4 = new QHBoxLayout;
    hbox4->setSpacing(KSpacing);
    hbox4->addStretch();
    hbox4->addWidget(editZonesBtn);
    hbox4->addStretch();
    QVBoxLayout* vbox2 = new QVBoxLayout;
    vbox2->addItem(hbox4);
    vbox2->addStretch();
    manualPage_->setLayout(vbox2);
    
    stackedWidget_ = new QStackedWidget;
    stackedWidget_->addWidget(aiPage_);
    stackedWidget_->addWidget(manualPage_);
    
    QVBoxLayout* vbox1 = new QVBoxLayout;
    vbox1->setSpacing(KSpacing);
    vbox1->addItem(hbox);
    vbox1->addItem(hbox1);
    vbox1->addItem(hbox2);
    vbox1->addWidget(stackedWidget_);
    
    this->setLayout(vbox1);
    
    QObject::connect(manualLeg_, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) {
            stackedWidget_->setCurrentWidget(manualPage_);
            if (!loadingConfig_) {
                emit fieldChanged(QStringLiteral("selected"), 0);
            }
        }
    });
    QObject::connect(AILeg_, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) {
            stackedWidget_->setCurrentWidget(aiPage_);
            if (!loadingConfig_) {
                emit fieldChanged(QStringLiteral("selected"), 1);
            }
        }
    });
    QObject::connect(manualLeg_, &QRadioButton::clicked, this, [this]() {
        reportBodyShapingLegModeSelect(app_, id(), to_u8(displayName()), u8"manual");
    });
    QObject::connect(AILeg_, &QRadioButton::clicked, this, [this]() {
        reportBodyShapingLegModeSelect(app_, id(), to_u8(displayName()), u8"ai");
    });
    QObject::connect(editZonesBtn, &QPushButton::clicked, this, [this]() {
        reportBodyShapingManualLegAction(app_,
                                         id(),
                                         to_u8(displayName()),
                                         u8"editor_open");
        manualPopup_->show();
    });
}

BodyShapingSolutionController::~BodyShapingSolutionController() {
    delete manualPopup_;
}

void BodyShapingSolutionController::bindSliderEditor(QSlider* slider, QLineEdit* editor, const char* key) {
    slider->setValue(0);
    editor->setText(QString::number(0));

    QObject::connect(slider, &QSlider::valueChanged, this, [this, editor, key](int value) {
        editor->setText(QString::number(value));
        if (!loadingConfig_) {
            emit fieldChanged(QString::fromUtf8(key), static_cast<uint32_t>(value));
        }
    });
    QObject::connect(editor, &QLineEdit::editingFinished, this, [this, slider, editor, key]() {
        const bool userEdited = editor->isModified();
        editor->setModified(false);
        int value = editor->text().toUInt();
        if (slider->value() != value) {
            slider->setValue(value);
            if (userEdited) {
                reportBodyShapingParamInput(app_,
                                            id(),
                                            to_u8(displayName()),
                                            to_u8(key),
                                            value);
            }
        } else if (!loadingConfig_) {
            emit fieldChanged(QString::fromUtf8(key), static_cast<uint32_t>(value));
        }
    });
}

void BodyShapingSolutionController::json2Me(const nlohmann::json& j) {
    loadingConfig_ = true;
    setId(uintAt(j, u8"id", 1));
    name_ = from_u8(u8StringAt(j, u8"customName"));
    uint32_t value = uintAt(j, u8"slimBody", kDefaultUiValue);
    slimBodySlider_->setValue(value);
    slimBodyEditor_->setText(QString("%1").arg(value));
    value = uintAt(j, u8"smallHead", kDefaultUiValue);
    smallHeadSlider_->setValue(value);
    smallHeadEditor_->setText(QString("%1").arg(value));
    uint32_t selected = uintAt(j, u8"selected");
    if (selected == 0) {
        manualLeg_->setChecked(true);
        AILeg_->setChecked(false);
    }
    else {
        assert(selected == 1);
        manualLeg_->setChecked(false);
        AILeg_->setChecked(true);
    }
    
    value = uintAt(j, u8"longLeg", kDefaultUiValue);
    longLegSlider_->setValue(value);
    longLegEditor_->setText(QString("%1").arg(value));
    
    manualPopup_->json2Me(objectAt(j, u8"manual"));
    loadingConfig_ = false;
}

BodyShapingSettingController::BodyShapingSettingController(GlobalModel* app, QWidget* parent/* = nullptr*/)
: CiciWidget(parent), app_(app) {
    assert(app_ != nullptr);
}

void BodyShapingSettingController::createUI() {
    QLabel* camera = createLabel(tr("Camera:"));
    QLabel* cameraName = createVideoInputLabel(app_, tr("No Camera"));
    
    QHBoxLayout* hbox = new QHBoxLayout;
    hbox->addWidget(camera);
    hbox->addWidget(cameraName);
    hbox->addStretch();
    
    enableBodyShaping_ = createCheckBox(tr("Enable body shaping"));
    QHBoxLayout* hbox1 = new QHBoxLayout;
    hbox1->addWidget(enableBodyShaping_);
    hbox1->addStretch();
    
    tabWidget_ = new QTabWidget;
    applyDisabledTabStyle(tabWidget_);
    tabWidget_->setTabsClosable(true);
    
    newSlnWidget_ = new QWidget;
    tabWidget_->addTab(newSlnWidget_, tr("+ New Sln"));
    refreshNewSolutionTabCloseButton();
    
    QVBoxLayout* vbox = new QVBoxLayout;
    vbox->addItem(hbox);
    vbox->addItem(hbox1);
    vbox->addWidget(tabWidget_);
    
    addScrollBar(vbox);
    
    QObject::connect(enableBodyShaping_, &QCheckBox::stateChanged, this, &BodyShapingSettingController::onEnableMakeupStateChanged);
    QObject::connect(enableBodyShaping_, &QCheckBox::clicked, this, [this](bool checked) {
        reportBodyShapingEnableToggle(app_, checked);
    });
    QObject::connect(tabWidget_, &QTabWidget::tabCloseRequested, this, &BodyShapingSettingController::onTabCloseRequested);
    QObject::connect(tabWidget_, &QTabWidget::tabBarClicked, this, &BodyShapingSettingController::onTabBarClicked);
    QObject::connect(tabWidget_, &QTabWidget::tabBarDoubleClicked, this, &BodyShapingSettingController::onTabBarDoubleClicked);
    QObject::connect(tabWidget_, &QTabWidget::currentChanged, this, &BodyShapingSettingController::onCurrentChanged);

    refreshEnableState();
}

BodyShapingSettingController::~BodyShapingSettingController() {
}

void BodyShapingSettingController::switchLanguage() {
}

void BodyShapingSettingController::refreshEnableState() {
    bool b = (enableBodyShaping_->checkState() == Qt::Checked);
    tabWidget_->setEnabled(b);
}

bool BodyShapingSettingController::isSolutionTab(int index) const {
    return index >= 0 &&
           index < tabWidget_->count() &&
           tabWidget_->widget(index) != newSlnWidget_;
}

int BodyShapingSettingController::solutionTabCount() const {
    assert(tabWidget_->indexOf(newSlnWidget_) == tabWidget_->count() - 1);
    return tabWidget_->count() - 1;
}

int BodyShapingSettingController::selectedSolutionIndexAfterRemove(int removedIndex) const {
    const int current = tabWidget_->currentIndex();
    if (isSolutionTab(current)) {
        return current;
    }

    const int remaining = solutionTabCount();
    if (remaining <= 0) {
        return -1;
    }

    return removedIndex < remaining ? removedIndex : remaining - 1;
}

void BodyShapingSettingController::refreshNewSolutionTabCloseButton() {
    const int index = tabWidget_->indexOf(newSlnWidget_);
    assert(index >= 0);
    if (index < 0) {
        return;
    }

    tabWidget_->tabBar()->setTabButton(index, QTabBar::LeftSide, nullptr);
    tabWidget_->tabBar()->setTabButton(index, QTabBar::RightSide, nullptr);
}

void BodyShapingSettingController::onEnableMakeupStateChanged(int s) {
    refreshEnableState();
    if (!loadingConfig_) {
        emit settingPatchRequested(JsonUtility::Patch::add(u8"/bodyShaping/enableBodyShaping", s == Qt::Checked));
    }
}

void BodyShapingSettingController::onTabCloseRequested(int index) {
    if (!isSolutionTab(index)) {
        assert(false);
        return;
    }

    auto* w = tabWidget_->widget(index);
    auto* solution = qobject_cast<BodyShapingSolutionController*>(w);
    assert(solution != nullptr);
    if (!loadingConfig_ && solution != nullptr) {
        reportBodyShapingSolutionAction(app_,
                                        u8"solution_remove",
                                        solution->id(),
                                        to_u8(solution->displayName()));
    }
    int selected = -1;
    {
        QSignalBlocker blocker(tabWidget_);
        tabWidget_->removeTab(index);
        selected = selectedSolutionIndexAfterRemove(index);
        assert(selected >= 0);
        tabWidget_->setCurrentIndex(selected);
        refreshNewSolutionTabCloseButton();
    }
    delete w;
    if (!loadingConfig_) {
        auto patch = JsonUtility::Patch::remove(std::u8string(u8"/bodyShaping/solutions/") + to_u8(index));
        JsonUtility::Patch::appendAdd(patch, u8"/bodyShaping/selected", selected);
        emit settingPatchRequested(patch);
    }
}

void BodyShapingSettingController::onTabBarClicked(int index) {
    auto widget = tabWidget_->widget(index);
    if (widget != newSlnWidget_) {
        auto* solution = qobject_cast<BodyShapingSolutionController*>(widget);
        assert(solution != nullptr);
        if (solution != nullptr) {
            reportBodyShapingSolutionAction(app_,
                                            u8"solution_select",
                                            solution->id(),
                                            to_u8(solution->displayName()));
        }
        return;
    }
    
    BodyShapingSolutionController* solutionTab = new BodyShapingSolutionController(app_);
    solutionTab->setId(uniqueSolutionId<BodyShapingSolutionController>(tabWidget_));
    const auto solutionJson = bodySolutionSkeleton(solutionTab->id());
    index = tabWidget_->count() - 1;
    assert(index >= 0);
    {
        QSignalBlocker blocker(tabWidget_);
        tabWidget_->insertTab(index, solutionTab, solutionTab->displayName());
        tabWidget_->setCurrentIndex(index);
        refreshNewSolutionTabCloseButton();
    }
    QObject::connect(solutionTab, &BodyShapingSolutionController::fieldChanged, this,
                     [this, solutionTab](const QString& key, const nlohmann::json& value) {
                         emitSolutionFieldPatch(solutionTab, key, value);
                     });
    QObject::connect(solutionTab, &BodyShapingSolutionController::fieldChangedWithAction, this,
                     [this, solutionTab](const QString& key,
                                         const nlohmann::json& value,
                                         const QString& uiAction) {
                         emitSolutionFieldPatch(solutionTab, key, value, uiAction);
                     });

    auto patch = JsonUtility::Patch::add(u8"/bodyShaping/solutions/-", solutionJson);
    JsonUtility::Patch::appendAdd(patch, u8"/bodyShaping/selected", index);
    emit settingPatchRequested(patch);
    solutionTab->json2Me(solutionJson);
    reportBodyShapingSolutionAction(app_,
                                    u8"solution_create",
                                    solutionTab->id(),
                                    to_u8(solutionTab->displayName()));
}

void BodyShapingSettingController::onTabBarDoubleClicked(int index) {
    if (!isSolutionTab(index)) {
        assert(false);
        return;
    }

    auto sln = qobject_cast<BodyShapingSolutionController*>(tabWidget_->widget(index));
    assert(sln != nullptr);

    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename Sln"), tr("Name:"),
                                               QLineEdit::Normal, sln->customName(), &ok);
    if (!ok) {
        return;
    }

    sln->setCustomName(name);
    tabWidget_->setTabText(index, sln->displayName());
    emit settingPatchRequested(JsonUtility::Patch::add(std::u8string(u8"/bodyShaping/solutions/")
                                                 + to_u8(index)
                                                 + u8"/customName",
                                                 to_json(name)));
    reportBodyShapingSolutionAction(app_,
                                    u8"solution_rename",
                                    sln->id(),
                                    to_u8(sln->displayName()));
}

void BodyShapingSettingController::onCurrentChanged(int index) {
    if (!loadingConfig_ && isSolutionTab(index)) {
        emit settingPatchRequested(JsonUtility::Patch::add(u8"/bodyShaping/selected", static_cast<uint32_t>(index)));
    }
}

void BodyShapingSettingController::reset() {
    resetState(true);
}

void BodyShapingSettingController::resetState(bool) {
	assert(tabWidget_ != nullptr);
    QSignalBlocker tabBlocker(tabWidget_);
    for (int i = tabWidget_->count() - 1; i >= 0; --i) {
        QWidget* w = tabWidget_->widget(i);
        if (w == newSlnWidget_) {
            continue;
        }
        tabWidget_->removeTab(i);
        delete w;
    }

    if (enableBodyShaping_ != nullptr) {
        QSignalBlocker blocker(enableBodyShaping_);
        enableBodyShaping_->setCheckState(Qt::Unchecked);
    }
    if (newSlnWidget_ != nullptr && tabWidget_->indexOf(newSlnWidget_) >= 0) {
        tabWidget_->setCurrentWidget(newSlnWidget_);
    }
    refreshNewSolutionTabCloseButton();

    refreshEnableState();
}

void BodyShapingSettingController::json2Me(const nlohmann::json& j) {
    loadingConfig_ = true;
    resetState(false);

    bool enableBodyShaping = boolAt(j, u8"enableBodyShaping");
    uint32_t selected = uintAt(j, u8"selected");
    enableBodyShaping_->setCheckState(enableBodyShaping ? Qt::Checked : Qt::Unchecked);
    refreshEnableState();
    
    const auto& arr = arrayAt(j, u8"solutions");
    assert(arr.is_array());
    for (auto i = 0; i < arr.size(); i++) {
        const auto& item = arr[i];
        BodyShapingSolutionController* sln = new BodyShapingSolutionController(app_);
        QObject::connect(sln, &BodyShapingSolutionController::fieldChanged, this,
                         [this, sln](const QString& key, const nlohmann::json& value) {
                             emitSolutionFieldPatch(sln, key, value);
                         });
        QObject::connect(sln, &BodyShapingSolutionController::fieldChangedWithAction, this,
                         [this, sln](const QString& key,
                                     const nlohmann::json& value,
                                     const QString& uiAction) {
                             emitSolutionFieldPatch(sln, key, value, uiAction);
                         });
        sln->json2Me(item);
        auto index = tabWidget_->count() - 1;
        assert(index >= 0);
        tabWidget_->insertTab(index, sln, sln->displayName());
    }

    refreshNewSolutionTabCloseButton();
    assert(!arr.empty());
    if (arr.empty()) {
        loadingConfig_ = false;
        return;
    }
    if (selected >= arr.size()) {
        selected = static_cast<uint32_t>(arr.size() - 1);
    }
    QSignalBlocker blocker(tabWidget_);
    tabWidget_->setCurrentIndex(static_cast<int>(selected));
    loadingConfig_ = false;
}

void BodyShapingSettingController::emitSolutionFieldPatch(BodyShapingSolutionController* solution,
                                                          const QString& key,
                                                          const nlohmann::json& value,
                                                          const QString& uiAction) {
    if (loadingConfig_) {
        return;
    }

    const int index = tabWidget_->indexOf(solution);
    if (index < 0 || tabWidget_->widget(index) == newSlnWidget_) {
        return;
    }

    auto patch = JsonUtility::Patch::add(std::u8string(u8"/bodyShaping/solutions/")
                                   + to_u8(index)
                                   + u8"/"
                                   + to_u8(key),
                                   value);
    if (!uiAction.isEmpty()) {
        patch.front()[to_char(u8"ui_action")] = to_char(to_u8(uiAction));
    }
    emit settingPatchRequested(patch);
}
