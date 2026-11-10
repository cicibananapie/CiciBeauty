// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include <assert.h>

#include <string>

#include <QLabel>
#include <QSlider>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QTabWidget>
#include <QSignalBlocker>
#include <QHBoxLayout>
#include <QTabBar>
#include <QVBoxLayout>

#include "nlohmann/json.hpp"
#include "assetsmgr.h"
#include "misc.h"
#include "commonui.h"
#include "effectitemwidget.h"
#include "filtermodelhelpers.h"
#include "imagetextwidget.h"
#include "gridmorelayout.h"
#include "makeupsettingcontroller.h"
#include "../effectcatalog.h"
#include "../i18nsupport.h"
#include "../qstringconv.h"
#include "../modelapp.h"
#include "jsonutility.h"
#include "../statistics/settingspageanalytics.h"

namespace {
constexpr uint32_t kEmergencyUiValue = 50;
constexpr int kMakeupCategoryLabelWidth = 80;

const char8_t* stylizedMakeupParamKey(uint32_t index) {
    static constexpr const char8_t* keys[] = {
        u8"lip",
        u8"eye",
        u8"eyelash",
        u8"blush",
        u8"trimming",
        u8"eyebrow",
        u8"con",
        u8"undereye shadow",
        u8"pinkbottom",
        u8"highlight",
        u8"decoration",
    };

    assert(index < sizeof(keys) / sizeof(keys[0]));
    return keys[index];
}

using JsonUtility::boolAt;
using JsonUtility::emptyObject;
using JsonUtility::objectAt;
using JsonUtility::uintAt;

std::u8string idPathToken(uint64_t id) {
    return to_u8(id);
}

std::u8string stylizedMakeupParamPath(uint64_t effectId, const char8_t* key) {
    return std::u8string(u8"/makeup/stylized/effects/") + idPathToken(effectId) + u8"/" + key;
}

std::u8string customizedMakeupStrengthPath(uint32_t categoryId, uint64_t effectId) {
    return std::u8string(u8"/makeup/customized/") + idPathToken(categoryId)
         + u8"/effects/" + idPathToken(effectId) + u8"/strength";
}

bool tryResolveEffectParamValue(GlobalModel* app, const std::u8string& path, uint32_t& result) {
    int value = 0;
    auto* model = app->filterModel();
    if (model->effectParamValue(path.c_str(), value) && value >= 0) {
        result = static_cast<uint32_t>(value);
        return true;
    }

    return false;
}

uint32_t resolvedEffectParamValue(GlobalModel* app, const std::u8string& path) {
    uint32_t result = 0;
    if (tryResolveEffectParamValue(app, path, result)) {
        return result;
    }

    assert(false);
    return kEmergencyUiValue;
}

bool tryActivateEffect(GlobalModel* app,
                       IEffectConflictResolver* conflictResolver,
                       uint64_t effectId) {
    assert(app != nullptr);
    assert(conflictResolver != nullptr);
    assert(effectId != 0);

    auto* model = app->filterModel();
    if (model == nullptr) {
        return false;
    }

    nlohmann::json disablePatch;
    if (model->canActivateEffect(effectId, disablePatch)) {
        return true;
    }
    if (disablePatch.empty()) {
        assert(false);
        return false;
    }
    return conflictResolver->resolveEffectConflicts(disablePatch);
}

}

StylizedMakeupController::StylizedMakeupController(
    GlobalModel *app,
    IEffectConflictResolver *conflictResolver,
    QWidget *parent /* = nullptr*/)
	: CiciWidget(parent), app_(app), conflictResolver_(conflictResolver) {
    assert(app_ != nullptr);
    assert(conflictResolver_ != nullptr);
    app_->registerFilterModelChangeObserver(this);
    auto* info = currentObsInfo(app_);
    if (info != nullptr) {
        info->registerObserver(this);
    }
    paramUI_.create();
    
    for (auto idx = 0; idx < paramUI_.kParamUINumber; idx++) {
        const auto& item = paramUI_.items_[idx];
        bindSliderEditor(item.slider_, item.editor_, idx);
    }
    
    gridMoreLayout_ = new GridMoreLayout;
    gridMoreLayout_->addItem(paramUI_.layout_);
    loadEffectList();
    gridMoreLayout_->setTroubleMaker(paramUI_.layout_);
    addScrollBar(gridMoreLayout_);
    hideParamUI();
}

StylizedMakeupController::~StylizedMakeupController() {
    auto* info = currentObsInfo(app_);
    if (info != nullptr) {
        info->unregisterObserver(this);
    }
    app_->unregisterFilterModelChangeObserver(this);
}

void StylizedMakeupController::onHVModeChanged(ObsInfo::HVMode mode) {
    for (auto w: widgets_) {
        const auto* effect = app_->effectCatalog()->effectInfo(w->id());
        assert(effect != nullptr);
        if (compatibleAtHVMode(effect->hv_, mode)) {
            w->show();
        }
        else {
            w->hide();
        }
    }
}

void StylizedMakeupController::onFilterModelChanged(FilterModel* oldModel, FilterModel* newModel) {
    if (oldModel != nullptr) {
        oldModel->obsInfo()->unregisterObserver(this);
    }
    if (newModel != nullptr) {
        newModel->obsInfo()->registerObserver(this);
    }

    auto* info = currentObsInfo(app_);
    if (info != nullptr) {
        onHVModeChanged(info->hvMode());
    }
}

void StylizedMakeupController::loadEffectList() {
    const auto& j = app_->effectCatalog()->stylizedMakeupJson();
    assert(!j.is_discarded());
    
    const auto& arr = j["list"];
    for (auto i = 0; i < arr.size(); i++) {
        const auto& item = arr[i];
        uint32_t id = item["id"];
        QString name = I18nSupport::localizedName(item);
        QString icon = from_json(item.value("iconpath", nlohmann::json()));
        auto* w = new EffectItemWidget(app_, this);
        w->setId(id);
        w->setText(name);
        w->setImage(icon);
        const auto* effect = app_->effectCatalog()->effectInfo(id);
        assert(effect != nullptr);
        if (effect != nullptr) {
            w->bindEffect(*effect);
        }
        
        gridMoreLayout_->addWidget(w);
        
        auto connection = QObject::connect(w, &ImageTextWidget::clicked, this, [this, w]() {
            onItemClicked(w);
            reportMakeupEffectSelect(app_,
                                     u8"stylized",
                                     std::nullopt,
                                     {},
                                     w->id(),
                                     to_u8(w->text()),
                                     selectedWidget_ == w);
        });
        assert(connection);
        if (auto* status = w->packageStatus(); status != nullptr) {
            QObject::connect(status,
                             &EffectPackageStatus::stateChanged,
                             this,
                             [this, w]() {
                                 if (selectedWidget_ != w) {
                                     return;
                                 }
                                 refreshSelectedParamUi();
                             });
        }
        widgets_.append(w);
    }

    auto* info = currentObsInfo(app_);
    if (info != nullptr) {
        onHVModeChanged(info->hvMode());
    }
}

void StylizedMakeupController::json2Me(const nlohmann::json& j) {
    loadingConfig_ = true;
    reset();

    uint32_t selected = uintAt(j, u8"selected");
    for (auto i = 0; i < widgets_.size(); i++) {
        auto* w = widgets_[i];
        if (selected == w->id()) {
            restoreFromConfig(w);
            break;
        }
    }

    loadingConfig_ = false;
}

void StylizedMakeupController::setEffectParam(uint32_t which, int value) {
    assert(which < paramUI_.kParamUINumber);
    if (selectedWidget_ == nullptr || !selectedWidget_->packageReady()) {
        return;
    }

    assert(paramUI_.effectId_ == selectedWidget_->id());
    if (!loadingConfig_) {
        const auto effectId = selectedWidget_->id();
        const QString key = QString("effects/%1/%2")
                            .arg(effectId)
                            .arg(from_u8(paramUI_.items_[which].paramName_));
        emit fieldChanged(key, static_cast<uint32_t>(value));
    }
}

void StylizedMakeupController::onItemClicked(EffectItemWidget* newWidget) {
	selectWidget(newWidget);
}

void StylizedMakeupController::bindSliderEditor(QSlider* slider, QLineEdit* editor, uint32_t cookie) {
    QObject::connect(slider, &QSlider::valueChanged, this, [this, editor, cookie](int value) {
        editor->setText(QString::number(value));
        setEffectParam(cookie, value);
    });
    QObject::connect(editor, &QLineEdit::editingFinished, this, [this, slider, editor, cookie]() {
        const bool userEdited = editor->isModified();
        editor->setModified(false);
        int value = editor->text().toUInt();
        if (slider->value() != value) {
            slider->setValue(value);
            if (userEdited && selectedWidget_ != nullptr) {
                reportMakeupParamInput(app_,
                                       u8"stylized",
                                       std::nullopt,
                                       {},
                                       selectedWidget_->id(),
                                       to_u8(selectedWidget_->text()),
                                       paramUI_.items_[cookie].paramName_,
                                       slider->value());
            }
        }
    });
}

void StylizedMakeupController::selectWidget(EffectItemWidget* newWidget) {
	assert(newWidget != nullptr);
    auto* target = selectedWidget_ == newWidget ? nullptr : newWidget;
	if (target != nullptr && !tryActivateEffect(app_, conflictResolver_, target->id())) {
        return;
    }
	if (selectedWidget_ != nullptr) {
		selectedWidget_->setSelected(false);
    }

    selectedWidget_ = target;
    gridMoreLayout_->hideTroubleMaker();
    hideParamUI();

    if (!loadingConfig_) {
        emit fieldChanged("selected", selectedWidget_ != nullptr ? selectedWidget_->id() : 0);
    }

    if (selectedWidget_ == nullptr) {
        refreshParamEnabledState();
        return;
    }

    selectedWidget_->setSelected(true);
    gridMoreLayout_->showTroubleMaker(selectedWidget_);
    refreshSelectedParamUi();
}

void StylizedMakeupController::restoreFromConfig(EffectItemWidget* newWidget) {
	if (selectedWidget_ != nullptr) {
		selectedWidget_->setSelected(false);
    }

    selectedWidget_ = newWidget;
    gridMoreLayout_->hideTroubleMaker();
    hideParamUI();

    if (selectedWidget_ == nullptr) {
        refreshParamEnabledState();
        return;
    }

    selectedWidget_->setSelected(true);
    gridMoreLayout_->showTroubleMaker(selectedWidget_);
    refreshSelectedParamUi();
}

void StylizedMakeupController::reset() {
    restoreFromConfig(nullptr);
    for (auto* w : widgets_) {
        w->setSelected(false);
    }
}

void StylizedMakeupController::hideParamUI() {
    paramUI_.effectId_ = 0;
    paramUI_.hide();
}

void StylizedMakeupController::showParamUI(uint32_t effectId) {
    paramUI_.effectId_ = effectId;

    QSignalBlocker blockers[DynamicParamUI::kParamUINumber] = {
        QSignalBlocker(paramUI_.items_[0].slider_),
        QSignalBlocker(paramUI_.items_[1].slider_),
        QSignalBlocker(paramUI_.items_[2].slider_),
        QSignalBlocker(paramUI_.items_[3].slider_),
        QSignalBlocker(paramUI_.items_[4].slider_),
        QSignalBlocker(paramUI_.items_[5].slider_),
        QSignalBlocker(paramUI_.items_[6].slider_),
        QSignalBlocker(paramUI_.items_[7].slider_),
        QSignalBlocker(paramUI_.items_[8].slider_),
        QSignalBlocker(paramUI_.items_[9].slider_),
        QSignalBlocker(paramUI_.items_[10].slider_),
    };
    (void)blockers;

    uint32_t visibleCount = 0;
    for (auto idx = 0u; idx < DynamicParamUI::kParamUINumber; idx++) {
        const auto* key = stylizedMakeupParamKey(idx);
        uint32_t value = 0;
        if (!tryResolveEffectParamValue(app_, stylizedMakeupParamPath(effectId, key), value)) {
            continue;
        }

        paramUI_.setParam(visibleCount,
                          to_u8(makeupType2String(idx + 1)),
                          std::u8string(key),
                          0.0f,
                          100.0f,
                          0.0f,
                          0);
        paramUI_.items_[visibleCount].setIntValue(value);
        visibleCount++;
    }
    paramUI_.show(visibleCount);
    refreshParamEnabledState();
}

void StylizedMakeupController::refreshSelectedParamUi() {
    if (selectedWidget_ == nullptr || !selectedWidget_->packageReady()) {
        hideParamUI();
        refreshParamEnabledState();
        return;
    }

    hideParamUI();
    showParamUI(selectedWidget_->id());
}

void StylizedMakeupController::refreshParamEnabledState() {
    paramUI_.setEnabled(selectedWidget_ != nullptr && selectedWidget_->packageReady());
}

QString StylizedMakeupController::makeupType2String(int type) const {
    switch (type) {
    case 1:
        return tr("lip");
    case 2:
        return tr("eye");
    case 3:
        return tr("eyelash");
    case 4:
        return tr("blush");
    case 5:
        return tr("trimming");
    case 6:
        return tr("eyebrow");
    case 7:
        return tr("con");
    case 8:
        return tr("undereye shadow");
    case 9:
        return tr("pinkbottom");
    case 10:
        return tr("highlight");
    case 11:
        return tr("decoration");
    default:
        return QString();
    }
}

MakeupCategoryWidget::MakeupCategoryWidget(
    GlobalModel *app,
    IEffectConflictResolver *conflictResolver,
    const nlohmann::json &j)
    : CiciWidget(nullptr), app_(app), conflictResolver_(conflictResolver) {
    assert(app_ != nullptr);
    assert(conflictResolver_ != nullptr);
    app_->registerFilterModelChangeObserver(this);
    auto* info = currentObsInfo(app_);
    if (info != nullptr) {
        info->registerObserver(this);
    }
    uint32_t id = j["id"];
    QString name = I18nSupport::localizedName(j);
    this->setId(id);
    QVBoxLayout* vbox = new QVBoxLayout;
    {
        QHBoxLayout* hbox = new QHBoxLayout;
        categoryNameLabel_ = createFixWidthMarqueeLabel(name, kMakeupCategoryLabelWidth);
        hbox->addWidget(categoryNameLabel_);
        hbox->addStretch();
        vbox->addItem(hbox);
    }
    {
        dynamicParamUI_.create();
        
        QObject::connect(dynamicParamUI_.slider_, &QSlider::valueChanged, this, &MakeupCategoryWidget::onSliderValueChanged);
        QObject::connect(dynamicParamUI_.editor_, &QLineEdit::editingFinished, this, &MakeupCategoryWidget::onEditingFinished);
        
        gridMoreLayout_ = new GridMoreLayout;
        gridMoreLayout_->addItem(dynamicParamUI_.layout_);
        gridMoreLayout_->setTroubleMaker(dynamicParamUI_.layout_);
        
        addWidgets(j["list"]);
        vbox->addItem(gridMoreLayout_);
    }
    
    this->setLayout(vbox);
    hideSlider();
}

MakeupCategoryWidget::~MakeupCategoryWidget() {
    auto* info = currentObsInfo(app_);
    if (info != nullptr) {
        info->unregisterObserver(this);
    }
    app_->unregisterFilterModelChangeObserver(this);
}

void MakeupCategoryWidget::onHVModeChanged(ObsInfo::HVMode mode) {
    for (auto w: widgets_) {
        const auto* effect = app_->effectCatalog()->effectInfo(w->id());
        assert(effect != nullptr);
        if (compatibleAtHVMode(effect->hv_, mode)) {
            w->show();
        }
        else {
            w->hide();
        }
    }
}

void MakeupCategoryWidget::onFilterModelChanged(FilterModel* oldModel, FilterModel* newModel) {
    if (oldModel != nullptr) {
        oldModel->obsInfo()->unregisterObserver(this);
    }
    if (newModel != nullptr) {
        newModel->obsInfo()->registerObserver(this);
    }

    auto* info = currentObsInfo(app_);
    if (info != nullptr) {
        onHVModeChanged(info->hvMode());
    }
}

void MakeupCategoryWidget::onSliderValueChanged(int value) {
    if (selectedWidget_ == nullptr || !selectedWidget_->packageReady()) {
        return;
    }

    dynamicParamUI_.setIntValue(value);
    if (!loadingConfig_) {
        emit strengthChanged(static_cast<uint32_t>(value));
    }
}

void MakeupCategoryWidget::onEditingFinished() {
    if (selectedWidget_ == nullptr || !selectedWidget_->packageReady()) {
        return;
    }

    const bool userEdited = dynamicParamUI_.editor_->isModified();
    dynamicParamUI_.editor_->setModified(false);
    int v = dynamicParamUI_.editor_->text().toInt();
    if (dynamicParamUI_.slider_->value() != v) {
        QSignalBlocker blocker(dynamicParamUI_.slider_);
        dynamicParamUI_.setIntValue(v);
        if (!loadingConfig_) {
            emit strengthChanged(static_cast<uint32_t>(v));
            if (userEdited) {
                reportMakeupStrengthInput(app_,
                                          id(),
                                          to_u8(categoryNameLabel_->text()),
                                          selectedWidget_->id(),
                                          to_u8(selectedWidget_->text()),
                                          v);
            }
        }
    }
}

void MakeupCategoryWidget::onItemClicked(EffectItemWidget* newWidget) {
	selectWidget(newWidget);
}

void MakeupCategoryWidget::selectWidget(EffectItemWidget* newWidget) {
	assert(newWidget != nullptr);
    auto* target = selectedWidget_ == newWidget ? nullptr : newWidget;
	if (target != nullptr && !tryActivateEffect(app_, conflictResolver_, target->id())) {
        return;
    }
	if (selectedWidget_ != nullptr) {
		selectedWidget_->setSelected(false);
	}

    selectedWidget_ = target;
    gridMoreLayout_->hideTroubleMaker();
    hideSlider();

    if (!loadingConfig_) {
        emit selectedChanged();
    }

    if (selectedWidget_ == nullptr) {
        refreshParamEnabledState();
        return;
    }

    selectedWidget_->setSelected(true);
    gridMoreLayout_->showTroubleMaker(selectedWidget_);
    refreshSelectedParamUi();
}

void MakeupCategoryWidget::restoreFromConfig(EffectItemWidget* newWidget) {
	if (selectedWidget_ != nullptr) {
		selectedWidget_->setSelected(false);
	}

    selectedWidget_ = newWidget;
    gridMoreLayout_->hideTroubleMaker();
    hideSlider();

    if (selectedWidget_ == nullptr) {
        refreshParamEnabledState();
        return;
    }

    selectedWidget_->setSelected(true);
    gridMoreLayout_->showTroubleMaker(selectedWidget_);
    refreshSelectedParamUi();
}

void MakeupCategoryWidget::hideSlider() {
    QSignalBlocker blocker(dynamicParamUI_.slider_);
    dynamicParamUI_.setIntValue(kEmergencyUiValue);
    dynamicParamUI_.hide();
}

void MakeupCategoryWidget::showSlider() {
    assert(selectedWidget_ != nullptr);
    std::u8string labelU8 = to_u8(tr("Strength"));
    QSignalBlocker blocker(dynamicParamUI_.slider_);
    dynamicParamUI_.setParam(labelU8, u8"Intensity", 0.0f, 100.0f, 0.0f, 0);
    dynamicParamUI_.setIntValue(resolvedEffectParamValue(app_,
                                                         customizedMakeupStrengthPath(id(),
                                                                                     selectedWidget_->id())));
    
    dynamicParamUI_.show();
    refreshParamEnabledState();
}

void MakeupCategoryWidget::refreshSelectedParamUi() {
    if (selectedWidget_ == nullptr || !selectedWidget_->packageReady()) {
        hideSlider();
        refreshParamEnabledState();
        return;
    }

    showSlider();
}

void MakeupCategoryWidget::refreshParamEnabledState() {
    dynamicParamUI_.setEnabled(selectedWidget_ != nullptr && selectedWidget_->packageReady());
}

void MakeupCategoryWidget::setName(const QString& name) {
    categoryNameLabel_->setText(name);
}

bool MakeupCategoryWidget::hasSelection() const {
    return selectedWidget_ != nullptr;
}

uint32_t MakeupCategoryWidget::selectedEffectId() const {
    return selectedWidget_ != nullptr ? selectedWidget_->id() : 0;
}

void MakeupCategoryWidget::addWidgets(const nlohmann::json& j) {
    assert(j.is_array());

    for (auto i = 0; i < j.size(); i++) {
        const auto& item = j[i];
        uint32_t id = item["id"];
        QString name = I18nSupport::localizedName(item);
        QString icon = from_json(item.value("iconpath", nlohmann::json()));
        auto* w = new EffectItemWidget(app_, this);
        w->setId(id);
        w->setText(name);
        w->setImage(icon);
        const auto* effect = app_->effectCatalog()->effectInfo(id);
        assert(effect != nullptr);
        if (effect != nullptr) {
            w->bindEffect(*effect);
        }
        gridMoreLayout_->addWidget(w);
        auto connection = QObject::connect(w, &ImageTextWidget::clicked, this, [this, w]() {
            onItemClicked(w);
            reportMakeupEffectSelect(app_,
                                     u8"customized",
                                     this->id(),
                                     to_u8(categoryNameLabel_->text()),
                                     w->id(),
                                     to_u8(w->text()),
                                     selectedWidget_ == w);
        });
        assert(connection);
        if (auto* status = w->packageStatus(); status != nullptr) {
            QObject::connect(status,
                             &EffectPackageStatus::stateChanged,
                             this,
                             [this, w]() {
                                 if (selectedWidget_ != w) {
                                     return;
                                 }
                                 refreshSelectedParamUi();
                             });
        }
        widgets_.append(w);
    }

    auto* info = currentObsInfo(app_);
    if (info != nullptr) {
        onHVModeChanged(info->hvMode());
    }
}

void MakeupCategoryWidget::json2Me(const nlohmann::json& j) {
    loadingConfig_ = true;
    reset();

    uint32_t selected = uintAt(j, u8"selected");
    for (auto i = 0; i < widgets_.size(); i++) {
        auto w = widgets_[i];
        if (selected == w->id()) {
            restoreFromConfig(w);
            break;
        }
    }
    loadingConfig_ = false;
}

void MakeupCategoryWidget::reset() {
    restoreFromConfig(nullptr);
    for (auto* w : widgets_) {
        w->setSelected(false);
    }
}

CustomizedMakeupController::CustomizedMakeupController(
    GlobalModel *app,
    IEffectConflictResolver *conflictResolver,
    QWidget *parent)
	: CiciWidget(parent), app_(app), conflictResolver_(conflictResolver) {
    assert(app_ != nullptr);
    assert(conflictResolver_ != nullptr);
    QVBoxLayout* vbox = new QVBoxLayout;
    loadEffectList(vbox);
    
    addScrollBar(vbox);
}

CustomizedMakeupController::~CustomizedMakeupController() {
    
}

void CustomizedMakeupController::json2Me(const nlohmann::json& j) {
    resetState(false);

    assert(j.is_object());
    const auto& source = j.is_object() ? j : emptyObject();
    for (auto* category : widgets_) {
        const auto it = source.find(to_char(idPathToken(category->id())));
        if (it != source.end() && it->is_object()) {
            category->json2Me(*it);
        } else {
            nlohmann::json defaultCategory = nlohmann::json::object();
            defaultCategory[to_char(u8"selected")] = 0;
            defaultCategory[to_char(u8"effects")] = nlohmann::json::object();
            category->json2Me(defaultCategory);
        }
    }
}

void CustomizedMakeupController::loadEffectList(QLayout* vbox) {
    const auto& j = app_->effectCatalog()->customizedMakeupJson();
    assert(!j.is_discarded());
    
    const auto& categories = j["categories"];
    assert(categories.is_array());
    for (auto i = 0; i < categories.size(); i++) {
        const auto& item = categories[i];
        auto category = new MakeupCategoryWidget(app_, conflictResolver_, item);
        QObject::connect(category, &MakeupCategoryWidget::selectedChanged, this, [this, category]() {
            onCategorySelectedChanged(category);
        });
        QObject::connect(category, &MakeupCategoryWidget::strengthChanged, this, [this, category](uint32_t value) {
            onCategoryStrengthChanged(category, value);
        });
        vbox->addWidget(category);
        widgets_.push_back(category);
    }
}

MakeupSettingController::MakeupSettingController(
    GlobalModel *app,
    IEffectConflictResolver *conflictResolver,
    QWidget *parent /* = nullptr*/)
	: CiciWidget(parent), app_(app), conflictResolver_(conflictResolver) {
    assert(app_ != nullptr);
    assert(conflictResolver_ != nullptr);
}

void MakeupSettingController::createUI() {
    QLabel* camera = createLabel(tr("Camera:"));
    QLabel* cameraName = createVideoInputLabel(app_, tr("No camera"));
    QHBoxLayout* hbox = new QHBoxLayout;
    hbox->addWidget(camera);
    hbox->addWidget(cameraName);
    hbox->addStretch();
    
    enableMakeup_ = createCheckBox(tr("Enable makeup"));
    
    QHBoxLayout* hbox1 = new QHBoxLayout;
    hbox1->addWidget(enableMakeup_);
    hbox1->addStretch();
    
    stylizedMakeup_ = new StylizedMakeupController(app_, conflictResolver_);
    customizedMakeup_ = new CustomizedMakeupController(app_, conflictResolver_);
    QObject::connect(stylizedMakeup_, &StylizedMakeupController::fieldChanged,
                     this, &MakeupSettingController::onStylizedFieldChanged);
    QObject::connect(customizedMakeup_, &CustomizedMakeupController::itemFieldChanged,
                     this, &MakeupSettingController::onCustomizedItemFieldChanged);
    tabWidget_ = new QTabWidget;
    applyDisabledTabStyle(tabWidget_);
    tabWidget_->addTab(stylizedMakeup_, tr("Stylized makeup"));
    tabWidget_->addTab(customizedMakeup_, tr("Customized makeup"));
    
    QVBoxLayout* vbox = new QVBoxLayout;
    vbox->addItem(hbox);
    vbox->addItem(hbox1);
    vbox->addWidget(tabWidget_, 100);
    
    addScrollBar(vbox);
    
    QObject::connect(enableMakeup_, &QCheckBox::stateChanged, this, &MakeupSettingController::onEnableMakeupStateChanged);
    QObject::connect(tabWidget_, &QTabWidget::currentChanged, this, &MakeupSettingController::onCurrentChanged);
    QObject::connect(enableMakeup_, &QCheckBox::clicked, this, [this](bool checked) {
        reportMakeupEnableToggle(app_, checked);
    });
    auto* makeupTabBar = tabWidget_->findChild<QTabBar*>();
    assert(makeupTabBar != nullptr);
    QObject::connect(makeupTabBar, &QTabBar::tabBarClicked, this, [this](int index) {
        if (index < 0 || index > 1) {
            return;
        }
        reportMakeupTypeSelect(app_, index == 0 ? u8"stylized" : u8"customized");
    });

    refreshEnableState();
}

void CustomizedMakeupController::reset() {
    resetState(true);
}

void CustomizedMakeupController::resetState(bool resetCategories) {
    if (!resetCategories) {
        return;
    }

    for (auto* category : widgets_) {
        category->reset();
    }
}

void CustomizedMakeupController::onCategorySelectedChanged(MakeupCategoryWidget* category) {
    assert(category != nullptr);

    emit itemFieldChanged(category->id(), "selected", category->selectedEffectId());
}

void CustomizedMakeupController::onCategoryStrengthChanged(MakeupCategoryWidget* category, uint32_t value) {
    assert(category != nullptr);
    if (!category->hasSelection()) {
        return;
    }

    const QString key = QString("effects/%1/strength").arg(category->selectedEffectId());
    emit itemFieldChanged(category->id(), key, value);
}

MakeupSettingController::~MakeupSettingController() {
}

void MakeupSettingController::switchLanguage() {
    
}

void MakeupSettingController::refreshEnableState() {
    bool enableMakeup = (enableMakeup_->checkState() == Qt::Checked);
    tabWidget_->setEnabled(enableMakeup);
}

void MakeupSettingController::onEnableMakeupStateChanged(int s) {
    if (loadingConfig_) {
        return;
    }

    emit settingPatchRequested(JsonUtility::Patch::add(u8"/makeup/enableMakeup", s == Qt::Checked));
    refreshEnableState();
}

void MakeupSettingController::onCurrentChanged(int index) {
    if (!loadingConfig_) {
        emit settingPatchRequested(JsonUtility::Patch::add(u8"/makeup/selected", static_cast<uint32_t>(index)));
    }
}

void MakeupSettingController::reset() {
    resetState(true);
}

void MakeupSettingController::resetState(bool resetChildren) {
    QSignalBlocker blocker(enableMakeup_);
    enableMakeup_->setCheckState(Qt::Unchecked);
    QSignalBlocker blocker2(tabWidget_);
    tabWidget_->setCurrentIndex(0);
    if (resetChildren) {
        stylizedMakeup_->reset();
        customizedMakeup_->reset();
    }
    refreshEnableState();
}

void MakeupSettingController::json2Me(const nlohmann::json& j) {
    loadingConfig_ = true;
    resetState(false);

    bool enableMakeup = boolAt(j, u8"enableMakeup");
    {
        QSignalBlocker blocker(enableMakeup_);
        enableMakeup_->setCheckState(enableMakeup?Qt::Checked:Qt::Unchecked);
    }

    uint32_t selected = uintAt(j, u8"selected");
    assert(selected < 2);
    {
        QSignalBlocker blocker(tabWidget_);
        tabWidget_->setCurrentIndex(static_cast<int>(selected));
    }
    stylizedMakeup_->json2Me(objectAt(j, u8"stylized"));
    customizedMakeup_->json2Me(objectAt(j, u8"customized"));
    refreshEnableState();
    loadingConfig_ = false;
}

void MakeupSettingController::onStylizedFieldChanged(const QString& key, const nlohmann::json& value) {
    if (loadingConfig_) {
        return;
    }

    emit settingPatchRequested(JsonUtility::Patch::add(std::u8string(u8"/makeup/stylized/") + to_u8(key), value));
}

void MakeupSettingController::onCustomizedItemFieldChanged(uint32_t categoryId,
                                                           const QString& key,
                                                           const nlohmann::json& value) {
    if (loadingConfig_) {
        return;
    }

    emit settingPatchRequested(JsonUtility::Patch::add(std::u8string(u8"/makeup/customized/")
                                                 + to_u8(categoryId)
                                                 + u8"/"
                                                 + to_u8(key),
                                                 value));
}
