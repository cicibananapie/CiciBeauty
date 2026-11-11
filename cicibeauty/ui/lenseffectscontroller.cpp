// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include <iterator>
#include <string_view>

#include <QLabel>
#include <QSlider>
#include <QLineEdit>
#include <QCheckBox>
#include <QTabWidget>
#include <QSignalBlocker>
#include <QHBoxLayout>
#include <QStackedLayout>
#include <QVBoxLayout>

#include "nlohmann/json.hpp"
#include "assetsmgr.h"
#include "misc.h"
#include "commonui.h"
#include "effectitemwidget.h"
#include "filtermodelhelpers.h"
#include "imagetextwidget.h"
#include "gridmorelayout.h"
#include "../effectcatalog.h"
#include "../modelapp.h"
#include "misc.h"
#include "../i18nsupport.h"
#include "../qstringconv.h"
#include "../settingconfig.h"
#include "lenseffectscontroller.h"
#include "jsonutility.h"
#include "../statistics/settingspageanalytics.h"

namespace {
constexpr uint32_t kDefaultUiValue = 50;
constexpr uint32_t kShotAtmosphereFavoritesTabId = 0;
constexpr std::u8string_view kShotAtmosphereFavoritePage = u8"shotAtmosphere";

using JsonUtility::arrayAt;
using JsonUtility::boolAt;
using JsonUtility::emptyObject;
using JsonUtility::objectAt;
using JsonUtility::uiValueAt;
using JsonUtility::uintAt;

std::u8string cameraMovementParamPath(uint32_t effectId, std::u8string_view key) {
    return std::u8string(u8"/lensEffects/cameraMovement/movements/")
        + to_u8(effectId)
        + u8"/"
        + std::u8string(key);
}

uint32_t resolvedCameraMovementUiValue(GlobalModel* app,
                                       uint32_t effectId,
                                       std::u8string_view key,
                                       uint32_t fallback) {
    int value = 0;
    auto* model = app != nullptr ? app->filterModel() : nullptr;
    const auto path = cameraMovementParamPath(effectId, key);
    if (model != nullptr && model->effectParamValue(path.c_str(), value) && value >= 0) {
        return static_cast<uint32_t>(value);
    }

    return fallback;
}

}
    
BasicLensEffectsController::BasicLensEffectsController(GlobalModel *app, QWidget *parent /* = nullptr*/)
	: CiciWidget(parent), app_(app) {
    assert(app_ != nullptr);
    app_->registerFilterModelChangeObserver(this);
    auto* info = currentObsInfo(app_);
    if (info != nullptr) {
        info->registerObserver(this);
    }
    QVBoxLayout* vbox = new QVBoxLayout;
    const QString squareImage = from_path(FileSystemOp::resourcesPath() / u8"square@1x.png");
    {
        QLabel* label = createLabel(tr("Split screen"));
        faceTracking_ = createCheckBox(tr("Face track"));
        QHBoxLayout* hbox = new QHBoxLayout;
        hbox->addWidget(label);
        hbox->addItem(createExpandingHorizontalSpacer());
        hbox->addWidget(faceTracking_);
        vbox->addItem(hbox);
        
        QObject::connect(faceTracking_, &QCheckBox::stateChanged, this, &BasicLensEffectsController::onFaceTrackStateChanged);
        QObject::connect(faceTracking_, &QCheckBox::clicked, this, [this](bool checked) {
            reportLensToggle(app_, u8"face_tracking_toggle", checked);
        });
    }
    {
        dualScreen_ = new ImageTextWidget(app_->assets());
        dualScreen_->setText(tr("Dual screen"));
        dualScreen_->setImage(squareImage);
        tripleScreen_ = new ImageTextWidget(app_->assets());
        tripleScreen_->setText(tr("Triple screen"));
        tripleScreen_->setImage(squareImage);
        QHBoxLayout* hbox = new QHBoxLayout;
        hbox->addWidget(dualScreen_);
        hbox->addWidget(tripleScreen_);
        hbox->addStretch();
        vbox->addItem(hbox);
        
        QObject::connect(dualScreen_, &ImageTextWidget::clicked, this, &BasicLensEffectsController::onDualScreenClicked);
        QObject::connect(tripleScreen_, &ImageTextWidget::clicked, this, &BasicLensEffectsController::onTripleScreenClicked);
    }
    {
        QLabel* label = createLabel(tr("Background blur"));
        vbox->addWidget(label);
    }
    {
        QHBoxLayout* hbox = new QHBoxLayout;
        bkBlur_ = new EffectItemWidget(app_);
        bkBlur_->setText(tr("Background blur"));
        bkBlur_->setImage(squareImage);
        hbox->addWidget(bkBlur_);
        hbox->addStretch();
        vbox->addItem(hbox);
        
        QObject::connect(bkBlur_, &ImageTextWidget::clicked, this, &BasicLensEffectsController::onbkBlurClicked);
    }
    
    vbox->addStretch();
    this->setLayout(vbox);
    
    loadEffectConfig();
}

BasicLensEffectsController::~BasicLensEffectsController() {
    auto* info = currentObsInfo(app_);
    if (info != nullptr) {
        info->unregisterObserver(this);
    }
    app_->unregisterFilterModelChangeObserver(this);
}

void BasicLensEffectsController::reset() {
    QSignalBlocker blocker(faceTracking_);
    faceTracking_->setCheckState(Qt::Unchecked);
    dualScreen_->setSelected(false);
    tripleScreen_->setSelected(false);
    bkBlur_->setSelected(false);
}

void BasicLensEffectsController::onHVModeChanged(ObsInfo::HVMode mode) {
    //assert(false);
}

void BasicLensEffectsController::onFilterModelChanged(FilterModel* oldModel, FilterModel* newModel) {
    if (oldModel != nullptr) {
        oldModel->obsInfo()->unregisterObserver(this);
    }
    if (newModel != nullptr) {
        newModel->obsInfo()->registerObserver(this);
    }
}

void BasicLensEffectsController::json2Me(const nlohmann::json& j) {
    reset();

    const auto& source = j.is_object() ? j : emptyObject();

    bool enableFaceTracking = boolAt(source, u8"enableFaceTracking");
    {
        QSignalBlocker blocker(faceTracking_);
        faceTracking_->setCheckState(enableFaceTracking?Qt::Checked:Qt::Unchecked);
    }
    uint32_t selectedSplit = uintAt(source, u8"selectedSplit");
    dualScreen_->setSelected(selectedSplit == dualScreen_->id());
    tripleScreen_->setSelected(selectedSplit == tripleScreen_->id());
    
    bool bkBlur = boolAt(source, u8"bkBlur");
    bkBlur_->setSelected(bkBlur);
}

void BasicLensEffectsController::loadEffectConfig() {
    const auto& j = app_->effectCatalog()->cameraEffectsJson();
    assert(!j.is_discarded());

    const auto& dualScreen = objectAt(j, u8"dualScreen");
    dualScreen_->setId(uintAt(dualScreen, u8"id"));
    dualScreen_->setText(I18nSupport::localizedName(dualScreen));
    dualScreen_->setImage(from_json(dualScreen.value("iconpath", nlohmann::json())));

    const auto& tripleScreen = objectAt(j, u8"tripleScreen");
    tripleScreen_->setId(uintAt(tripleScreen, u8"id"));
    tripleScreen_->setText(I18nSupport::localizedName(tripleScreen));
    tripleScreen_->setImage(from_json(tripleScreen.value("iconpath", nlohmann::json())));

    const auto& bkBlur = objectAt(j, u8"bkBlur");
    const auto bkBlurEffectId = uintAt(bkBlur, u8"id");
    bkBlur_->setId(bkBlurEffectId);
    bkBlur_->setText(I18nSupport::localizedName(bkBlur));
    bkBlur_->setImage(from_json(bkBlur.value("iconpath", nlohmann::json())));
    const auto* bkBlurEffect = app_->effectCatalog()->effectInfo(bkBlurEffectId);
    assert(bkBlurEffect != nullptr);
    if (bkBlurEffect != nullptr) {
        bkBlur_->bindEffect(*bkBlurEffect);
    }

    const auto& faceTracking = objectAt(j, u8"faceTracking");
    faceTracking_->setText(I18nSupport::localizedName(faceTracking));

    assert(dualScreen_->id() != 0);
    assert(tripleScreen_->id() != 0);
    assert(uintAt(faceTracking, u8"id") != 0);
    assert(app_->effectCatalog()->effectInfo(dualScreen_->id()) != nullptr);
    assert(app_->effectCatalog()->effectInfo(tripleScreen_->id()) != nullptr);
    assert(app_->effectCatalog()->effectInfo(uintAt(faceTracking, u8"id")) != nullptr);
}


void BasicLensEffectsController::onFaceTrackStateChanged(int s) {
    emit fieldChanged("enableFaceTracking", s == Qt::Checked);
}

void BasicLensEffectsController::onDualScreenClicked() {
    if (dualScreen_->isSelected()) {
        dualScreen_->setSelected(false);
    }
    else {
        dualScreen_->setSelected(true);
        tripleScreen_->setSelected(false);
    }
    
    if (dualScreen_->isSelected()) {
        assert(!tripleScreen_->isSelected());
    }
    else if (tripleScreen_->isSelected()) {
        assert(!dualScreen_->isSelected());
    }
    uint32_t selectedSplit = dualScreen_->isSelected() ? dualScreen_->id()
                           : tripleScreen_->isSelected() ? tripleScreen_->id()
                           : 0;
    emit fieldChanged("selectedSplit", selectedSplit);
    reportLensEffectSelect(app_,
                           u8"basic",
                           dualScreen_->id(),
                           to_u8(dualScreen_->text()),
                           dualScreen_->isSelected(),
                           u8"basic");
}

void BasicLensEffectsController::onTripleScreenClicked() {
    if (tripleScreen_->isSelected()) {
        tripleScreen_->setSelected(false);
    }
    else {
        tripleScreen_->setSelected(true);
        dualScreen_->setSelected(false);
    }
    if (dualScreen_->isSelected()) {
        assert(!tripleScreen_->isSelected());
    }
    
    else if (tripleScreen_->isSelected()) {
        assert(!dualScreen_->isSelected());
    }
    uint32_t selectedSplit = dualScreen_->isSelected() ? dualScreen_->id()
                           : tripleScreen_->isSelected() ? tripleScreen_->id()
                           : 0;
    emit fieldChanged("selectedSplit", selectedSplit);
    reportLensEffectSelect(app_,
                           u8"basic",
                           tripleScreen_->id(),
                           to_u8(tripleScreen_->text()),
                           tripleScreen_->isSelected(),
                           u8"basic");
}

void BasicLensEffectsController::onbkBlurClicked() {
    if (bkBlur_->isSelected()) {
        bkBlur_->setSelected(false);
    }
    else {
        bkBlur_->setSelected(true);
    }
    emit fieldChanged("bkBlur", bkBlur_->isSelected());
    reportLensEffectSelect(app_,
                           u8"basic",
                           bkBlur_->id(),
                           to_u8(bkBlur_->text()),
                           bkBlur_->isSelected(),
                           u8"basic");
}

CameraMovementController::CameraMovementController(GlobalModel* app, QWidget* parent/* = nullptr*/)
: CiciWidget(parent), app_(app) {
    assert(app_ != nullptr);
    app_->registerFilterModelChangeObserver(this);
    auto* info = currentObsInfo(app_);
    if (info != nullptr) {
        info->registerObserver(this);
    }
    gridMoreLayout_ = new GridMoreLayout;
    {
	    dynamicUI_.create();

	    for (auto idx = 0; idx < dynamicUI_.kParamUINumber; idx++) {
		    const auto &item = dynamicUI_.items_[idx];
		    bindSliderEditor(item.slider_, item.editor_, idx);
	    }
        gridMoreLayout_->addItem(dynamicUI_.layout_);
	    gridMoreLayout_->setTroubleMaker(dynamicUI_.layout_);
    }
    loadEffectList();
    
    this->setLayout(gridMoreLayout_);
    
    hideSlider();
}

CameraMovementController::~CameraMovementController() {
    auto* info = currentObsInfo(app_);
    if (info != nullptr) {
        info->unregisterObserver(this);
    }
    app_->unregisterFilterModelChangeObserver(this);
}

void CameraMovementController::onHVModeChanged(ObsInfo::HVMode mode) {
	for (auto w : widgets_) {
        const auto* effect = app_->effectCatalog()->effectInfo(w->id());
        assert(effect != nullptr);
		if (compatibleAtHVMode(effect->hv_, mode)) {
			w->show();
		} else {
			w->hide();
		}
	}
}

void CameraMovementController::onFilterModelChanged(FilterModel* oldModel, FilterModel* newModel) {
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

void CameraMovementController::bindSliderEditor(QSlider *slider, QLineEdit *editor, uint32_t cookie) {
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
                reportLensParamInput(app_,
                                     u8"camera_movement",
                                     selectedWidget_->id(),
                                     to_u8(selectedWidget_->text()),
                                     cookie == 0 ? u8"speed" : u8"amplitude",
                                     value);
            }
        }
    });
}

void CameraMovementController::onItemClicked(EffectItemWidget* newWidget) {
	selectWidget(newWidget);
}

void CameraMovementController::setEffectParam(uint32_t which, int value) {
    assert(which < dynamicUI_.kParamUINumber);
    if (selectedWidget_ == nullptr || !selectedWidget_->packageReady()) {
        return;
    }

    assert(dynamicUI_.effectId_ == selectedWidget_->id());
    const auto id = dynamicUI_.effectId_;
    auto& movement = movements_[id];
    const char* key = nullptr;
    if (which == 0) {
        movement.speed = value;
        key = "speed";
    } else if (which == 1) {
        movement.amplitude = value;
        key = "amplitude";
    } else {
        assert(false);
        return;
    }
    if (!loadingConfig_) {
        emit movementFieldChanged(id, key, static_cast<uint32_t>(value));
    }
}

void CameraMovementController::loadEffectList() {
    const auto& j = app_->effectCatalog()->cameraMovementJson();
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
        widgets_.push_back(w);
        
        QObject::connect(w, &ImageTextWidget::clicked, this, [this, w]() {
            onItemClicked(w);
            reportLensEffectSelect(app_,
                                   u8"camera_movement",
                                   w->id(),
                                   to_u8(w->text()),
                                   selectedWidget_ == w,
                                   u8"camera_movement");
        });
        if (auto* status = w->packageStatus(); status != nullptr) {
            QObject::connect(status,
                             &EffectPackageStatus::stateChanged,
                             this,
                             [this, w]() {
                                 if (selectedWidget_ != w) {
                                     return;
                                 }
                                 if (w->packageReady()) {
                                     showSlider();
                                 } else {
                                     refreshParamEnabledState();
                                 }
                             });
        }
    }

    auto* info = currentObsInfo(app_);
    if (info != nullptr) {
        onHVModeChanged(info->hvMode());
    }
}

void CameraMovementController::json2Me(const nlohmann::json& j) {
    loadingConfig_ = true;
    reset();

    const auto& source = j.is_object() ? j : emptyObject();

    uint32_t selected = uintAt(source, u8"selected");
    const auto& movements = objectAt(source, u8"movements");
    for (auto* widget : widgets_) {
        const auto id = widget->id();
        auto movementIt = movements_.find(id);
        assert(movementIt != movements_.end());
        if (movementIt == movements_.end()) {
            continue;
        }

        const auto configIt = movements.find(std::to_string(id));
        if (configIt == movements.end() || !configIt->is_object()) {
            continue;
        }

        auto& movement = movementIt->second;
        (void)uiValueAt(*configIt, u8"speed", movement.speed);
        (void)uiValueAt(*configIt, u8"amplitude", movement.amplitude);
    }
    
    for (auto i = 0; i < widgets_.size(); i++) {
        auto w = widgets_[i];
        if (selected == w->id()) {
            restoreFromConfig(w);
            break;
        }
    }
    loadingConfig_ = false;
}

void CameraMovementController::reset() {
    restoreFromConfig(nullptr);
    movements_.clear();
    for (auto* w : widgets_) {
        w->setSelected(false);
        const auto id = w->id();
        movements_[id] = {kDefaultUiValue, kDefaultUiValue};
    }
}

void CameraMovementController::hideSlider() {
	dynamicUI_.effectId_ = 0;
	dynamicUI_.hide();
}

void CameraMovementController::showSlider() {
	assert(selectedWidget_ != nullptr);
	auto id = selectedWidget_->id();
	dynamicUI_.effectId_ = id;
    static const char8_t* paramNames[] = {u8"Speed", u8"Intensity"};
    const QString uiNames[] = {tr("Speed"), tr("Intensity")};
    QSignalBlocker speedBlocker(dynamicUI_.items_[0].slider_);
    QSignalBlocker amplitudeBlocker(dynamicUI_.items_[1].slider_);
    for (auto i = 0; i < sizeof(paramNames)/sizeof(paramNames[0]); i++) {
        dynamicUI_.setParam(i, to_u8(uiNames[i]), std::u8string(paramNames[i]), 0.0f, 100.0f, 0.0f, 0);
    }

    auto& movement = movements_[id];
    movement.speed = resolvedCameraMovementUiValue(app_, id, u8"speed", movement.speed);
    movement.amplitude = resolvedCameraMovementUiValue(app_, id,
                                                       u8"amplitude",
                                                       movement.amplitude);
    const auto speed = movement.speed;
    const auto amplitude = movement.amplitude;
    dynamicUI_.items_[0].setIntValue(speed);
    dynamicUI_.items_[1].setIntValue(amplitude);
    
	dynamicUI_.show(sizeof(paramNames)/sizeof(paramNames[0]));
    refreshParamEnabledState();
}

void CameraMovementController::refreshParamEnabledState() {
    dynamicUI_.setEnabled(selectedWidget_ != nullptr && selectedWidget_->packageReady());
}

void CameraMovementController::selectWidget(EffectItemWidget* newWidget) {
	assert(newWidget != nullptr);
    restoreFromConfig(selectedWidget_ == newWidget ? nullptr : newWidget);
    if (!loadingConfig_) {
        emit selectedChanged(selectedWidget_ != nullptr ? selectedWidget_->id() : 0);
    }
}

void CameraMovementController::restoreFromConfig(EffectItemWidget* newWidget) {
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
    showSlider();
}

ShotAtmosphereController::ShotAtmosphereController(GlobalModel *app, QWidget *parent /* = nullptr*/)
	: QTabWidget(parent), app_(app) {
    assert(app_ != nullptr);
    applyDisabledTabStyle(this);
    app_->registerFilterModelChangeObserver(this);
    auto* info = currentObsInfo(app_);
    if (info != nullptr) {
        info->registerObserver(this);
    }
    {
        auto* favoritesPage = new CiciWidget;
        favoritesPage->setId(kShotAtmosphereFavoritesTabId);
        favoritesStack_ = new QStackedLayout;
        favoritesPage->setLayout(favoritesStack_);

        auto* favoritesGridPage = new CiciWidget;
        favoritesGridLayout_ = new GridMoreLayout;
        favoritesGridPage->addScrollBar(favoritesGridLayout_);
        favoritesStack_->addWidget(favoritesGridPage);

        auto* noFavoritesPage = new QWidget;
        auto* noFavoritesLayout = new QVBoxLayout;
        auto* noFavoritesLabel = createLabel(tr("No favorites"));
        noFavoritesLabel->setAlignment(Qt::AlignCenter);
        noFavoritesLayout->addStretch();
        noFavoritesLayout->addWidget(noFavoritesLabel);
        noFavoritesLayout->addStretch();
        noFavoritesPage->setLayout(noFavoritesLayout);
        favoritesStack_->addWidget(noFavoritesPage);
        favoritesStack_->setCurrentIndex(1);

        this->addTab(favoritesPage, tr("Favorites"));
    }
    
    loadEffectList();
    QObject::connect(this, &QTabWidget::currentChanged, this, &ShotAtmosphereController::onCurrentChanged);
    QObject::connect(this, &QTabWidget::tabBarClicked, this, [this](int index) {
        if (index < 0 || index >= count()) {
            return;
        }
        auto* category = static_cast<CiciWidget*>(widget(index));
        reportLensCategorySelect(app_,
                                 category != nullptr ? category->id() : kShotAtmosphereFavoritesTabId,
                                 to_u8(tabText(index)));
    });
}

ShotAtmosphereController::~ShotAtmosphereController() {
    auto* info = currentObsInfo(app_);
    if (info != nullptr) {
        info->unregisterObserver(this);
    }
    app_->unregisterFilterModelChangeObserver(this);
}

void ShotAtmosphereController::onHVModeChanged(ObsInfo::HVMode mode) {
    for (auto& item : effectItems_) {
        const auto effectId = item.originalWidget_->id();
        const auto* effect = app_->effectCatalog()->effectInfo(effectId);
        assert(effect != nullptr);
        const bool compatible = effect != nullptr && compatibleAtHVMode(effect->hv_, mode);
        item.originalWidget_->setVisible(compatible);
        if (item.favoriteWidget_ != nullptr) {
            item.favoriteWidget_->setVisible(compatible);
        }
    }
}

void ShotAtmosphereController::onFilterModelChanged(FilterModel* oldModel, FilterModel* newModel) {
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

void ShotAtmosphereController::onItemClicked(EffectItemWidget* w) {
    auto* item = effectItem(w->id());
    assert(item != nullptr);
    if (item == nullptr) {
        return;
    }

    setSelectedWidget(selectedWidget_ == item->originalWidget_ ? nullptr : item->originalWidget_);
    if (!loadingConfig_) {
        emit fieldChanged("selectedEffect", selectedWidget_ != nullptr ? selectedWidget_->id() : 0);
        reportLensEffectSelect(app_,
                               u8"shot_atmosphere",
                               w->id(),
                               to_u8(w->text()),
                               selectedWidget_ != nullptr,
                               currentIndex() == 0 ? u8"favorites" : u8"category");
    }
}

void ShotAtmosphereController::onCurrentChanged(int index) {
    if (loadingConfig_ || index < 0) {
        return;
    }

    auto* current = static_cast<CiciWidget*>(widget(index));
    assert(current != nullptr);
    emit fieldChanged("selectedTab",
                      current != nullptr ? current->id() : kShotAtmosphereFavoritesTabId);
}

void ShotAtmosphereController::loadEffectList() {
    const auto& j = app_->effectCatalog()->shotAtmosphereJson();
    assert(!j.is_discarded());
    
    const auto& categories = j["categories"];
    assert(categories.is_array());
    for (auto i = 0; i < categories.size(); i++) {
        const auto& category = categories[i];
        uint32_t id = category["id"];
        QString name = I18nSupport::localizedName(category);
        CiciWidget* w = new CiciWidget;
        w->setId(id);
        GridMoreLayout* grid = new GridMoreLayout;
        const auto& arr = category["list"];
        assert(arr.is_array());
        for (auto k = 0; k < arr.size(); k++) {
            const auto& item = arr[k];
            uint32_t id = item["id"];
            QString name = I18nSupport::localizedName(item);
            QString icon = from_json(item.value("iconpath", nlohmann::json()));
            auto* imgWidget = new EffectItemWidget(app_);
            imgWidget->setId(id);
            imgWidget->setText(name);
            imgWidget->setImage(icon);
            const auto* effect = app_->effectCatalog()->effectInfo(id);
            assert(effect != nullptr);
            if (effect != nullptr) {
                imgWidget->bindEffect(*effect);
            }
            grid->addWidget(imgWidget);
            QObject::connect(imgWidget, &ImageTextWidget::clicked, this, [this, imgWidget]() {
                onItemClicked(imgWidget);
            });
            QObject::connect(imgWidget, &ImageTextWidget::liked, this,
                             [this, imgWidget](bool favorite) {
                setEffectFavorite(imgWidget->id(), favorite);
            });
            effectItems_.push_back({imgWidget, nullptr, icon});
        }
        w->addScrollBar(grid);
        this->addTab(w, name);
    }

    auto* info = currentObsInfo(app_);
    if (info != nullptr) {
        onHVModeChanged(info->hvMode());
    }

    reloadFavorites();
}

void ShotAtmosphereController::setSelectedWidget(EffectItemWidget* w) {
    if (selectedWidget_ != nullptr) {
        auto* oldItem = effectItem(selectedWidget_->id());
        assert(oldItem != nullptr);
        if (oldItem != nullptr) {
            oldItem->originalWidget_->setSelected(false);
            if (oldItem->favoriteWidget_ != nullptr) {
                oldItem->favoriteWidget_->setSelected(false);
            }
        }
    }

    selectedWidget_ = w;
    if (selectedWidget_ == nullptr) {
        return;
    }

    auto* item = effectItem(selectedWidget_->id());
    assert(item != nullptr);
    if (item != nullptr) {
        item->originalWidget_->setSelected(true);
        if (item->favoriteWidget_ != nullptr) {
            item->favoriteWidget_->setSelected(true);
        }
    }
}

void ShotAtmosphereController::setEffectFavorite(uint32_t effectId, bool favorite) {
    auto* item = effectItem(effectId);
    assert(item != nullptr);
    if (item == nullptr) {
        return;
    }

    reportLensFavoriteToggle(app_,
                             effectId,
                             to_u8(item->originalWidget_->text()),
                             favorite);

    if (!app_->settingConfig()->setFavorite(kShotAtmosphereFavoritePage, effectId, favorite)) {
        const bool current = item->favoriteWidget_ != nullptr;
        item->originalWidget_->setFavorite(current);
        if (item->favoriteWidget_ != nullptr) {
            item->favoriteWidget_->setFavorite(current);
        }
        return;
    }

    applyEffectFavorite(*item, favorite);
}

void ShotAtmosphereController::applyEffectFavorite(EffectItemViews& item, bool favorite) {
    item.originalWidget_->setFavorite(favorite);
    if (favorite == (item.favoriteWidget_ != nullptr)) {
        if (item.favoriteWidget_ != nullptr) {
            item.favoriteWidget_->setFavorite(favorite);
        }
        return;
    }

    if (favorite) {
        auto* widget = new EffectItemWidget(app_);
        widget->setId(item.originalWidget_->id());
        widget->setText(item.originalWidget_->text());
        widget->setImage(item.iconPath_);
        const auto* effect = app_->effectCatalog()->effectInfo(widget->id());
        assert(effect != nullptr);
        if (effect != nullptr) {
            widget->bindEffect(*effect);
        }
        widget->setFavorite(true);
        widget->setSelected(item.originalWidget_ == selectedWidget_);
        QObject::connect(widget, &ImageTextWidget::clicked, this,
                         [this, originalWidget = item.originalWidget_]() {
            onItemClicked(originalWidget);
        });
        QObject::connect(widget, &ImageTextWidget::liked, this,
                         [this, effectId = widget->id()](bool value) {
            setEffectFavorite(effectId, value);
        });

        item.favoriteWidget_ = widget;
        favoritesGridLayout_->addWidget(widget);
        widget->setVisible(isEffectCompatible(widget->id()));
        ++favoriteCount_;
        favoritesStack_->setCurrentIndex(0);
        return;
    }

    auto* widget = item.favoriteWidget_;
    item.favoriteWidget_ = nullptr;
    widget->hide();
    favoritesGridLayout_->removeWidget(widget);
    widget->deleteLater();
    --favoriteCount_;
    assert(favoriteCount_ >= 0);
    if (favoriteCount_ == 0) {
        favoritesStack_->setCurrentIndex(1);
    }
}

void ShotAtmosphereController::reloadFavorites() {
    for (auto& item : effectItems_) {
        applyEffectFavorite(
            item,
            app_->settingConfig()->isFavorite(kShotAtmosphereFavoritePage,
                                               item.originalWidget_->id()));
    }
}

ShotAtmosphereController::EffectItemViews*
ShotAtmosphereController::effectItem(uint32_t effectId) {
    for (auto& item : effectItems_) {
        if (item.originalWidget_->id() == effectId) {
            return &item;
        }
    }

    return nullptr;
}

bool ShotAtmosphereController::isEffectCompatible(uint32_t effectId) const {
    const auto* effect = app_->effectCatalog()->effectInfo(effectId);
    assert(effect != nullptr);
    if (effect == nullptr) {
        return false;
    }

    auto* info = currentObsInfo(app_);
    return info == nullptr || compatibleAtHVMode(effect->hv_, info->hvMode());
}

void ShotAtmosphereController::reset() {
    setSelectedWidget(nullptr);
    for (auto& item : effectItems_) {
        item.originalWidget_->setSelected(false);
        if (item.favoriteWidget_ != nullptr) {
            item.favoriteWidget_->setSelected(false);
        }
    }

    QSignalBlocker blocker(this);
    setCurrentIndex(0);
}

void ShotAtmosphereController::json2Me(const nlohmann::json& j) {
    loadingConfig_ = true;
    reset();

    const auto& source = j.is_object() ? j : emptyObject();

    uint32_t selectedTab = uintAt(source, u8"selectedTab");
    for (auto i = 0; i < count(); i++) {
        CiciWidget* w = static_cast<CiciWidget*>(widget(i));
        if (selectedTab == w->id()) {
            setCurrentIndex(i);
            break;
        }
    }
    
    uint32_t selectedEffect = uintAt(source, u8"selectedEffect");
    auto* selectedItem = effectItem(selectedEffect);
    if (selectedItem != nullptr) {
        setSelectedWidget(selectedItem->originalWidget_);
    }
    loadingConfig_ = false;
}

LensEffectsController::LensEffectsController(GlobalModel *app, QWidget *parent /* = nullptr*/)
	: CiciWidget(parent), app_(app) {
}

void LensEffectsController::createUI() {
    QVBoxLayout* vbox = new QVBoxLayout;
    {
        QLabel* camera = createLabel(tr("Camera:"));
        QLabel* cameraName = createVideoInputLabel(app_, tr("No Camera"));
        QHBoxLayout* hbox = new QHBoxLayout;
        hbox->addWidget(camera);
        hbox->addWidget(cameraName);
        hbox->addStretch();
        vbox->addItem(hbox);
    }
    {
        tabWidget_ = new QTabWidget;
        applyDisabledTabStyle(tabWidget_);
        basicLens_ = new BasicLensEffectsController(app_);
        tabWidget_->addTab(basicLens_, tr("Basic effects"));
        cameraMovement_ = new CameraMovementController(app_);
        tabWidget_->addTab(cameraMovement_, tr("Camera movement"));
        shotAtmosphere_ = new ShotAtmosphereController(app_);
        tabWidget_->addTab(shotAtmosphere_, tr("Shot atmosphere"));
        QObject::connect(basicLens_, &BasicLensEffectsController::fieldChanged,
                         this, &LensEffectsController::onBasicLensFieldChanged);
        QObject::connect(cameraMovement_, &CameraMovementController::selectedChanged,
                         this, &LensEffectsController::onCameraMovementSelectedChanged);
        QObject::connect(cameraMovement_, &CameraMovementController::movementFieldChanged,
                         this, &LensEffectsController::onCameraMovementFieldChanged);
        QObject::connect(shotAtmosphere_, &ShotAtmosphereController::fieldChanged,
                         this, &LensEffectsController::onShotAtmosphereFieldChanged);
        vbox->addWidget(tabWidget_, 100);
        
        QObject::connect(tabWidget_, &QTabWidget::currentChanged, this, &LensEffectsController::onCurrentChanged);
        QObject::connect(tabWidget_, &QTabWidget::tabBarClicked, this, [this](int index) {
            constexpr std::u8string_view views[] = {
                u8"basic", u8"camera_movement", u8"shot_atmosphere",
            };
            if (index < 0 || index >= static_cast<int>(std::size(views))) {
                return;
            }
            reportLensViewSelect(app_, views[index]);
        });
    }
    
    this->setLayout(vbox);
}

LensEffectsController::~LensEffectsController() {
    
}

void LensEffectsController::switchLanguage() {
}

void LensEffectsController::reloadFavorites() {
    if (shotAtmosphere_ != nullptr) {
        shotAtmosphere_->reloadFavorites();
    }
}

void LensEffectsController::onCurrentChanged(int index) {
    if (!loadingConfig_) {
        emit settingPatchRequested(JsonUtility::Patch::add(u8"/lensEffects/selected", static_cast<uint32_t>(index)));
    }
}

void LensEffectsController::reset() {
    resetState(true);
}

void LensEffectsController::resetState(bool resetChildren) {
    QSignalBlocker blocker(tabWidget_);
    tabWidget_->setCurrentIndex(0);

    if (resetChildren) {
        basicLens_->reset();
        cameraMovement_->reset();
        shotAtmosphere_->reset();
    }
}

void LensEffectsController::json2Me(const nlohmann::json& j) {
    loadingConfig_ = true;
    resetState(false);

    const auto& source = j.is_object() ? j : emptyObject();

    uint32_t selected = uintAt(source, u8"selected");
    if (selected >= static_cast<uint32_t>(tabWidget_->count())) {
        selected = 0;
    }
    {
        QSignalBlocker blocker(tabWidget_);
        tabWidget_->setCurrentIndex(static_cast<int>(selected));
    }
    basicLens_->json2Me(objectAt(source, u8"basicLens"));
    cameraMovement_->json2Me(objectAt(source, u8"cameraMovement"));
    shotAtmosphere_->json2Me(objectAt(source, u8"shotAtmosphere"));
    loadingConfig_ = false;
}

void LensEffectsController::onBasicLensFieldChanged(const QString& key, const nlohmann::json& value) {
    if (loadingConfig_) {
        return;
    }

    emit settingPatchRequested(JsonUtility::Patch::add(std::u8string(u8"/lensEffects/basicLens/") + to_u8(key), value));
}

void LensEffectsController::onCameraMovementSelectedChanged(uint32_t value) {
    if (loadingConfig_) {
        return;
    }

    emit settingPatchRequested(JsonUtility::Patch::add(u8"/lensEffects/cameraMovement/selected", value));
}

void LensEffectsController::onCameraMovementFieldChanged(uint32_t effectId,
                                                         const QString& key,
                                                         const nlohmann::json& value) {
    if (loadingConfig_) {
        return;
    }

    emit settingPatchRequested(JsonUtility::Patch::add(std::u8string(u8"/lensEffects/cameraMovement/movements/")
                                                 + to_u8(effectId)
                                                 + u8"/"
                                                 + to_u8(key),
                                                 value));
}

void LensEffectsController::onShotAtmosphereFieldChanged(const QString& key,
                                                         const nlohmann::json& value) {
    if (loadingConfig_) {
        return;
    }

    emit settingPatchRequested(JsonUtility::Patch::add(std::u8string(u8"/lensEffects/shotAtmosphere/") + to_u8(key), value));
}
