// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include <assert.h>
#include <string>
#include <QLabel>
#include <QSlider>
#include <QLineEdit>
#include <QCheckBox>
#include <QSignalBlocker>
#include <QHBoxLayout>
#include <QStackedLayout>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

#include "nlohmann/json.hpp"
#include "assetsmgr.h"
#include "commonui.h"
#include "filtermodelhelpers.h"
#include "effectitemwidget.h"
#include "filtersettingcontroller.h"
#include "gridmorelayout.h"
#include "../effectcatalog.h"
#include "../i18nsupport.h"
#include "../qstringconv.h"
#include "../modelapp.h"
#include "../settingconfig.h"
#include "jsonutility.h"
#include "../statistics/settingspageanalytics.h"

namespace {
constexpr std::u8string_view kFavoritePage = u8"filter";
constexpr int kFavoritesTab = 0;
constexpr int kAllTab = 1;

using JsonUtility::boolAt;
using JsonUtility::uintAt;

std::u8string idPathToken(uint64_t id) {
    return to_u8(id);
}

std::u8string filterStrengthPath(uint64_t effectId) {
    return std::u8string(u8"/filters/effects/") + idPathToken(effectId) + u8"/strength";
}

}

FilterSettingController::FilterSettingController(
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
}

void FilterSettingController::createUI() {
    QLabel* camera = createLabel(tr("Camera:"));
    QLabel* cameraName = createVideoInputLabel(app_, tr("No Camera"));
    
    QHBoxLayout* titleHbox = new QHBoxLayout;
    titleHbox->addWidget(camera);
    titleHbox->addWidget(cameraName);
    titleHbox->addStretch();
    
    enableFilter_ = createCheckBox(tr("Enable filter"));
    
    QHBoxLayout* hbox1 = new QHBoxLayout;
    hbox1->addWidget(enableFilter_);
    hbox1->addStretch();
    
    effectTabs_ = new QTabWidget;
    applyDisabledTabStyle(effectTabs_);

    auto* allPage = new QWidget;
    gridMoreLayout_ = new GridMoreLayout;
    dynamicParamUI_.create();
    gridMoreLayout_->addItem(dynamicParamUI_.layout_);
    gridMoreLayout_->setTroubleMaker(dynamicParamUI_.layout_);
    allPage->setLayout(gridMoreLayout_);

    auto* favoritesPage = new QWidget;
    favoritesStack_ = new QStackedLayout;
    favoritesPage->setLayout(favoritesStack_);

    auto* favoritesGridPage = new QWidget;
    favoritesGridLayout_ = new GridMoreLayout;
    favoriteDynamicParamUI_.create();
    favoritesGridLayout_->addItem(favoriteDynamicParamUI_.layout_);
    favoritesGridLayout_->setTroubleMaker(favoriteDynamicParamUI_.layout_);
    favoritesGridPage->setLayout(favoritesGridLayout_);
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

    effectTabs_->addTab(favoritesPage, tr("Favorites"));
    effectTabs_->addTab(allPage, tr("All"));
    loadEffectList();
    
    QVBoxLayout* vbox = new QVBoxLayout;
    vbox->addItem(titleHbox);
    vbox->addItem(hbox1);
    vbox->addWidget(effectTabs_, 100);
    
    addScrollBar(vbox);
    
    QObject::connect(dynamicParamUI_.slider_, &QSlider::valueChanged, this, &FilterSettingController::onSliderValueChanged);
    QObject::connect(dynamicParamUI_.editor_, &QLineEdit::editingFinished, this, &FilterSettingController::onEditingFinished);
    QObject::connect(favoriteDynamicParamUI_.slider_, &QSlider::valueChanged, this, &FilterSettingController::onSliderValueChanged);
    QObject::connect(favoriteDynamicParamUI_.editor_, &QLineEdit::editingFinished, this, &FilterSettingController::onEditingFinished);
    QObject::connect(enableFilter_, &QCheckBox::stateChanged, this, &FilterSettingController::onEnableFilterStateChanged);
    QObject::connect(effectTabs_, &QTabWidget::currentChanged, this, &FilterSettingController::onCurrentTabChanged);
    QObject::connect(enableFilter_, &QCheckBox::clicked, this, [this](bool checked) {
        reportFilterEnableToggle(app_, checked);
    });
    auto* effectTabBar = effectTabs_->findChild<QTabBar*>();
    assert(effectTabBar != nullptr);
    QObject::connect(effectTabBar, &QTabBar::tabBarClicked, this, [this](int index) {
        if (index != kFavoritesTab && index != kAllTab) {
            return;
        }
        reportFilterViewSelect(app_, index == kFavoritesTab ? u8"favorites" : u8"all");
    });
    
    hideSlider();
    refreshEnableState();
}

FilterSettingController::~FilterSettingController() {
    auto* info = currentObsInfo(app_);
    if (info != nullptr) {
        info->unregisterObserver(this);
    }
    app_->unregisterFilterModelChangeObserver(this);
}

void FilterSettingController::onHVModeChanged(ObsInfo::HVMode mode) {
    for (auto& item : effectItems_) {
        const auto* effect = app_->effectCatalog()->effectInfo(item.allWidget_->id());
        assert(effect != nullptr);
        const bool compatible = compatibleAtHVMode(effect->hv_, mode);
        item.allWidget_->setVisible(compatible);
        if (item.favoriteWidget_ != nullptr) {
            item.favoriteWidget_->setVisible(compatible);
        }
    }
}

void FilterSettingController::onFilterModelChanged(FilterModel* oldModel, FilterModel* newModel) {
    ++paramQueryId_;
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

void FilterSettingController::switchLanguage() {
    
}

void FilterSettingController::onSliderValueChanged(int value) {
    if (loadingConfig_ || selectedWidget_ == nullptr || !selectedWidget_->packageReady()) {
        return;
    }

    QSignalBlocker allBlocker(dynamicParamUI_.slider_);
    QSignalBlocker favoriteBlocker(favoriteDynamicParamUI_.slider_);
    dynamicParamUI_.setIntValue(value);
    favoriteDynamicParamUI_.setIntValue(value);
    emit settingPatchRequested(JsonUtility::Patch::add(filterStrengthPath(selectedWidget_->id()), value));
}

void FilterSettingController::onEditingFinished() {
    if (loadingConfig_ || selectedWidget_ == nullptr || !selectedWidget_->packageReady()) {
        return;
    }

    auto* editor = qobject_cast<QLineEdit*>(sender());
    assert(editor == dynamicParamUI_.editor_ || editor == favoriteDynamicParamUI_.editor_);
    const bool userEdited = editor->isModified();
    editor->setModified(false);
    const int value = editor->text().toInt();
    if (dynamicParamUI_.slider_->value() != value
        || favoriteDynamicParamUI_.slider_->value() != value) {
        QSignalBlocker allBlocker(dynamicParamUI_.slider_);
        QSignalBlocker favoriteBlocker(favoriteDynamicParamUI_.slider_);
        dynamicParamUI_.setIntValue(value);
        favoriteDynamicParamUI_.setIntValue(value);
        emit settingPatchRequested(JsonUtility::Patch::add(filterStrengthPath(selectedWidget_->id()), value));
        if (userEdited) {
            reportFilterStrengthInput(app_,
                                      selectedWidget_->id(),
                                      to_u8(selectedWidget_->text()),
                                      value);
        }
    }
}

void FilterSettingController::onEnableFilterStateChanged(int state) {
    if (loadingConfig_) {
        return;
    }

    const bool enabling = state == Qt::Checked;
    if (enabling && selectedWidget_ != nullptr) {
        conflictResolver_->confirmEffectActivation(this, selectedWidget_->id(), [this](bool allowed) {
            if (allowed) {
                emit settingPatchRequested(JsonUtility::Patch::add(u8"/filters/enableFilter", true));
            } else {
                QSignalBlocker blocker(enableFilter_);
                enableFilter_->setCheckState(Qt::Unchecked);
            }
            refreshEnableState();
        });
    } else {
	    emit settingPatchRequested(JsonUtility::Patch::add(u8"/filters/enableFilter", enabling));
    }
    refreshEnableState();
}

void FilterSettingController::onCurrentTabChanged(int index) {
    if (loadingConfig_ || index < 0) {
        return;
    }

    assert(index == kFavoritesTab || index == kAllTab);
    emit settingPatchRequested(JsonUtility::Patch::add(u8"/filters/selectedTab", index));
}

void FilterSettingController::refreshEnableState() {
    bool enableFilter = (enableFilter_->checkState() == Qt::Checked) ? true : false;
    for (auto& item : effectItems_) {
        item.allWidget_->setEnabled(enableFilter);
        if (item.favoriteWidget_ != nullptr) {
            item.favoriteWidget_->setEnabled(enableFilter);
        }
    }
    refreshParamEnabledState();
}

void FilterSettingController::refreshParamEnabledState() {
    const bool enabled = enableFilter_->checkState() == Qt::Checked
        && selectedWidget_ != nullptr
        && selectedWidget_->packageReady();
    dynamicParamUI_.setEnabled(enabled);
    favoriteDynamicParamUI_.setEnabled(enabled);
}

void FilterSettingController::selectWidget(EffectItemWidget* newWidget, std::u8string_view source) {
    assert(newWidget != nullptr);
    EffectItemWidget* selected = selectedWidget_ == newWidget ? nullptr : newWidget;
    auto complete = [this, newWidget, selected, source = std::u8string(source)](bool allowed) {
        if (allowed) {
            applySelection(selected);
        }
        reportFilterEffectSelect(app_, newWidget->id(), to_u8(newWidget->text()),
                                 selectedWidget_ == newWidget, source);
    };
    if (selected != nullptr && enableFilter_->checkState() == Qt::Checked) {
        conflictResolver_->confirmEffectActivation(newWidget, selected->id(), std::move(complete));
    } else {
        complete(true);
    }
}

void FilterSettingController::applySelection(EffectItemWidget* selected) {
    setSelectedWidget(selected);
    emit settingPatchRequested(JsonUtility::Patch::add(u8"/filters/selected", selected == nullptr ? 0 : selected->id()));
    refreshSelectedParamUi();
}

void FilterSettingController::setSelectedWidget(EffectItemWidget* newWidget) {
    if (selectedWidget_ == newWidget && newWidget != nullptr) {
        return;
    }

    if (selectedWidget_ != nullptr) {
        selectedWidget_->setSelected(false);
        auto* item = effectItem(selectedWidget_->id());
        assert(item != nullptr);
        if (item->favoriteWidget_ != nullptr) {
            item->favoriteWidget_->setSelected(false);
        }
    }

    selectedWidget_ = newWidget;
    gridMoreLayout_->hideTroubleMaker();
    favoritesGridLayout_->hideTroubleMaker();
    hideSlider();

    if (selectedWidget_ == nullptr) {
        refreshParamEnabledState();
        return;
    }

    selectedWidget_->setSelected(true);
    gridMoreLayout_->showTroubleMaker(selectedWidget_);
    auto* item = effectItem(selectedWidget_->id());
    assert(item != nullptr);
    if (item->favoriteWidget_ != nullptr) {
        item->favoriteWidget_->setSelected(true);
        favoritesGridLayout_->showTroubleMaker(item->favoriteWidget_);
    }
}

void FilterSettingController::restoreFromConfig(EffectItemWidget* newWidget) {
    setSelectedWidget(newWidget);
    refreshSelectedParamUi();
}

void FilterSettingController::refreshSelectedParamUi() {
    if (selectedWidget_ == nullptr || !selectedWidget_->packageReady()) {
        hideSlider();
        refreshParamEnabledState();
        return;
    }

    showSlider();
    refreshParamEnabledState();
}

void FilterSettingController::hideSlider() {
    ++paramQueryId_;
    dynamicParamUI_.hide();
    favoriteDynamicParamUI_.hide();
}

void FilterSettingController::showSlider() {
    assert(selectedWidget_ != nullptr);
    hideSlider();
    auto* model = app_->filterModel();
    if (model == nullptr) {
        return;
    }
    const auto effectId = selectedWidget_->id();
    const auto path = filterStrengthPath(effectId);
    model->queryEffectParamValues(
        this, {path}, [this, queryId = paramQueryId_, effectId, path](const nlohmann::json& values) {
            if (queryId != paramQueryId_) {
                return;
            }
            const auto value = values.find(to_char(path));
            if (value == values.end() || value->get<int>() < 0) {
                return;
            }
            const auto strength = value->get<int>();
            const auto label = to_u8(tr("Strength"));
            QSignalBlocker allBlocker(dynamicParamUI_.slider_);
            QSignalBlocker favoriteBlocker(favoriteDynamicParamUI_.slider_);
            dynamicParamUI_.setParam(label, u8"Intensity", strength, 100, 0, 0);
            favoriteDynamicParamUI_.setParam(label, u8"Intensity", strength, 100, 0, 0);
            dynamicParamUI_.show();
            auto* item = effectItem(effectId);
            assert(item != nullptr);
            if (item->favoriteWidget_ != nullptr) {
                favoriteDynamicParamUI_.show();
            }
            refreshParamEnabledState();
        });
}

void FilterSettingController::setEffectFavorite(uint64_t effectId, bool favorite) {
    auto* item = effectItem(effectId);
    assert(item != nullptr);

    if (!app_->settingConfig()->setFavorite(kFavoritePage, effectId, favorite)) {
        const bool current = item->favoriteWidget_ != nullptr;
        item->allWidget_->setFavorite(current);
        if (item->favoriteWidget_ != nullptr) {
            item->favoriteWidget_->setFavorite(current);
        }
        return;
    }

    applyEffectFavorite(*item, favorite);
    refreshSelectedParamUi();
}

void FilterSettingController::applyEffectFavorite(EffectItemViews& item, bool favorite) {
    item.allWidget_->setFavorite(favorite);
    if (favorite == (item.favoriteWidget_ != nullptr)) {
        return;
    }

    if (favorite) {
        auto* widget = new EffectItemWidget(app_);
        widget->setId(item.allWidget_->id());
        widget->setText(item.allWidget_->text());
        widget->setImage(item.iconPath_);
        const auto* effect = app_->effectCatalog()->effectInfo(widget->id());
        assert(effect != nullptr);
        widget->bindEffect(*effect);
        widget->setFavorite(true);
        widget->setSelected(item.allWidget_ == selectedWidget_);
        widget->setEnabled(enableFilter_->checkState() == Qt::Checked);
        QObject::connect(widget, &ImageTextWidget::clicked, this, [this, allWidget = item.allWidget_]() {
            selectWidget(allWidget, u8"favorites");
        });
        QObject::connect(widget, &ImageTextWidget::liked, this, [this, allWidget = item.allWidget_](bool value) {
            reportFilterFavoriteToggle(app_,
                                       allWidget->id(),
                                       to_u8(allWidget->text()),
                                       value);
            setEffectFavorite(allWidget->id(), value);
        });
        item.favoriteWidget_ = widget;
        favoritesGridLayout_->addWidget(widget);
        widget->setVisible(isEffectCompatible(widget->id()));
        if (item.allWidget_ == selectedWidget_) {
            favoritesGridLayout_->showTroubleMaker(widget);
        }
        ++favoriteCount_;
        favoritesStack_->setCurrentIndex(0);
        return;
    }

    auto* widget = item.favoriteWidget_;
    item.favoriteWidget_ = nullptr;
    if (item.allWidget_ == selectedWidget_) {
        favoritesGridLayout_->hideTroubleMaker();
        favoriteDynamicParamUI_.hide();
    }
    widget->hide();
    favoritesGridLayout_->removeWidget(widget);
    widget->deleteLater();
    --favoriteCount_;
    assert(favoriteCount_ >= 0);
    if (favoriteCount_ == 0) {
        favoritesStack_->setCurrentIndex(1);
    }
}

void FilterSettingController::reloadFavorites() {
    for (auto& item : effectItems_) {
        applyEffectFavorite(
            item,
            app_->settingConfig()->isFavorite(kFavoritePage, item.allWidget_->id()));
    }
    refreshSelectedParamUi();
}

void FilterSettingController::loadEffectList() {
    const auto& j = app_->effectCatalog()->filtersJson();
    assert(!j.is_discarded());
    
    const auto& list = j["list"];
    for (auto i = 0; i < list.size(); i++) {
        const auto& item = list[i];
        auto* w = new EffectItemWidget(app_);
        w->setId(item["id"]);
        w->setText(I18nSupport::localizedName(item));
        const QString iconPath = from_json(item.value("iconpath", nlohmann::json()));
        w->setImage(iconPath);
        const auto* effect = app_->effectCatalog()->effectInfo(w->id());
        assert(effect != nullptr);
        w->bindEffect(*effect);
        gridMoreLayout_->addWidget(w);
        
        auto connection = QObject::connect(w, &ImageTextWidget::clicked, this, [this, w]() {
            selectWidget(w);
        });
        assert(connection);
        QObject::connect(w, &ImageTextWidget::liked, this, [this, w](bool favorite) {
            reportFilterFavoriteToggle(app_,
                                       w->id(),
                                       to_u8(w->text()),
                                       favorite);
            setEffectFavorite(w->id(), favorite);
        });
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
        effectItems_.append({w, nullptr, iconPath});
        applyEffectFavorite(
            effectItems_.last(),
            app_->settingConfig()->isFavorite(kFavoritePage, w->id()));
    }

    auto* info = currentObsInfo(app_);
    if (info != nullptr) {
        onHVModeChanged(info->hvMode());
    }
}

void FilterSettingController::json2Me(const nlohmann::json& j) {
    loadingConfig_ = true;
    reset();

    bool enableFilter = boolAt(j, u8"enableFilter");
    {
        QSignalBlocker blocker(enableFilter_);
        enableFilter_->setCheckState(enableFilter ? Qt::Checked: Qt::Unchecked);
    }
    refreshEnableState();

    const auto selectedTab = uintAt(j, u8"selectedTab");
    assert(selectedTab == kFavoritesTab || selectedTab == kAllTab);
    effectTabs_->setCurrentIndex(selectedTab == kAllTab ? kAllTab : kFavoritesTab);

    const auto selected = uintAt(j, u8"selected");
    for (auto& item : effectItems_) {
        if (selected == item.allWidget_->id()) {
            restoreFromConfig(item.allWidget_);
            break;
        }
    }

    loadingConfig_ = false;
}

void FilterSettingController::reset() {
    QSignalBlocker blocker(enableFilter_);
    enableFilter_->setCheckState(Qt::Unchecked);

    restoreFromConfig(nullptr);

    for (auto& item : effectItems_) {
        item.allWidget_->setSelected(false);
        if (item.favoriteWidget_ != nullptr) {
            item.favoriteWidget_->setSelected(false);
        }
    }

    refreshEnableState();
}

FilterSettingController::EffectItemViews* FilterSettingController::effectItem(uint64_t effectId) {
    for (auto& item : effectItems_) {
        if (item.allWidget_->id() == effectId) {
            return &item;
        }
    }
    return nullptr;
}

bool FilterSettingController::isEffectCompatible(uint64_t effectId) const {
    auto* info = currentObsInfo(app_);
    if (info == nullptr) {
        return true;
    }

    const auto* effect = app_->effectCatalog()->effectInfo(effectId);
    assert(effect != nullptr);
    return compatibleAtHVMode(effect->hv_, info->hvMode());
}
