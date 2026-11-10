// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include <algorithm>
#include <string_view>

#include <QLabel>
#include <QCheckBox>
#include <QTabWidget>
#include <QSignalBlocker>
#include <QHBoxLayout>
#include <QStackedLayout>
#include <QTabBar>
#include <QVBoxLayout>

#include "nlohmann/json.hpp"
#include "commonui.h"
#include "effectitemwidget.h"
#include "filtermodelhelpers.h"
#include "gridmorelayout.h"
#include "specialeffectsassetscontroller.h"
#include "../effectcatalog.h"
#include "../i18nsupport.h"
#include "../qstringconv.h"
#include "../modelapp.h"
#include "../settingconfig.h"
#include "jsonutility.h"
#include "../statistics/settingspageanalytics.h"

namespace {

constexpr std::u8string_view kFavoritePage = u8"spEffects";
constexpr uint32_t kFavoritesPageId = 0;

std::u8string spEffectSelectedPath(uint32_t categoryIndex) {
    return std::u8string(u8"/spEffects/categories/")
         + to_u8(categoryIndex)
         + u8"/selected";
}

}

SpCategoryWidget::SpCategoryWidget(GlobalModel *app, QWidget *parent /* = nullptr*/) : CiciWidget(parent), app_(app) {
    assert(app_ != nullptr);
    app_->registerFilterModelChangeObserver(this);
    auto* info = currentObsInfo(app_);
    if (info != nullptr) {
        info->registerObserver(this);
    }
}

SpCategoryWidget::~SpCategoryWidget() {
    auto* info = currentObsInfo(app_);
	if (info != nullptr) {
		info->unregisterObserver(this);
	}
    app_->unregisterFilterModelChangeObserver(this);
}

void SpCategoryWidget::onHVModeChanged(ObsInfo::HVMode mode) {
    for (auto w: widgets_) {
        const auto* effect = app_->effectCatalog()->effectInfo(w->id());
        assert(effect != nullptr);
        const bool compatible = effect != nullptr && compatibleAtHVMode(effect->hv_, mode);
        w->setVisible(compatible);
    }
}

void SpCategoryWidget::onFilterModelChanged(FilterModel* oldModel, FilterModel* newModel) {
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

void SpCategoryWidget::addEffects(const nlohmann::json& j) {
    assert(j.is_array());

    GridMoreLayout* grid = new GridMoreLayout;
    for (auto i = 0; i < j.size(); i++) {
        const auto& item = j[i];
        uint32_t id = item["id"];
        QString text = I18nSupport::localizedName(item);
        QString icon = from_json(item.value("iconpath", nlohmann::json()));
        auto* w = new EffectItemWidget(app_, this);
        w->setId(id);
        w->setText(text);
        w->setImage(icon);
        const auto* effect = app_->effectCatalog()->effectInfo(id);
        assert(effect != nullptr);
        if (effect != nullptr) {
            w->bindEffect(*effect);
        }
        grid->addWidget(w);
        widgets_.push_back(w);
        
        QObject::connect(w, &ImageTextWidget::clicked, this, [this, w]() {
            selectWidget(w);
        });
        QObject::connect(w, &ImageTextWidget::liked, this, [this, w](bool favorite) {
            emit favoriteChanged(w->id(), favorite);
        });
    }

    auto* info = currentObsInfo(app_);
    if (info != nullptr) {
        onHVModeChanged(info->hvMode());
    }
    
    addScrollBar(grid);
}

void SpCategoryWidget::selectEffect(uint32_t effectId) {
    for (auto* w : widgets_) {
        if (w->id() == effectId) {
            selectWidget(w);
            return;
        }
    }

    assert(false);
}

void SpCategoryWidget::setEffectFavorite(uint32_t effectId, bool favorite) {
    for (auto* w : widgets_) {
        if (w->id() == effectId) {
            w->setFavorite(favorite);
            return;
        }
    }

    assert(false);
}

uint32_t SpCategoryWidget::selectedEffectId() const {
    return selectedWidget_ != nullptr ? selectedWidget_->id() : 0;
}

void SpCategoryWidget::reset() {
    if (selectedWidget_ != nullptr) {
        selectedWidget_->setSelected(false);
        selectedWidget_ = nullptr;
    }
    for (auto* w : widgets_) {
        w->setSelected(false);
    }
}

void SpCategoryWidget::json2Me(const nlohmann::json& j) {
    reset();

    const auto empty = nlohmann::json::object();
    const auto& source = j.is_object() ? j : empty;
    if (source.value("id", id()) != id()) {
        assert(false);
    }

    uint32_t selected = source.value("selected", 0);
    for (auto* w : widgets_) {
        if (selected == w->id()) {
            selectWidget(w);
            break;
        }
    }
}

nlohmann::json SpCategoryWidget::me2Json() const {
    auto j = nlohmann::json::object();
    j["id"] = id();
    j["selected"] = selectedWidget_ != nullptr ? selectedWidget_->id() : 0;
    
    return j;
}

void SpCategoryWidget::selectWidget(ImageTextWidget* w) {
    if (selectedWidget_ == nullptr) {
        selectedWidget_ = w;
        selectedWidget_->setSelected(true);
    }
    else {
        if (selectedWidget_ == w) {
            selectedWidget_->setSelected(false);
            selectedWidget_ = nullptr;
        }
        else {
            selectedWidget_->setSelected(false);
            selectedWidget_ = w;
            selectedWidget_->setSelected(true);
        }
    }

    emit selectionChanged(w->id(), selectedWidget_ != nullptr ? selectedWidget_->id() : 0);
}
SpecialEffectsAssetsController::SpecialEffectsAssetsController(GlobalModel *app, QWidget *parent /* = nullptr*/)
	: CiciWidget(parent), app_(app) {
    assert(app_ != nullptr);
    app_->registerFilterModelChangeObserver(this);
    auto* info = currentObsInfo(app_);
    if (info != nullptr) {
        info->registerObserver(this);
    }
}

void SpecialEffectsAssetsController::createUI() {
    QVBoxLayout* vbox = new QVBoxLayout;
    {
        QLabel* label = createLabel(tr("Camera:"));
        cameraName_ = createVideoInputLabel(app_, tr("No Camera"));
        QHBoxLayout* hbox = new QHBoxLayout;
        hbox->addWidget(label);
        hbox->addWidget(cameraName_);
        hbox->addStretch();
        
        vbox->addItem(hbox);
    }
    {
        enableAudio_ = createCheckBox(tr("Turn on the sound"));
        vbox->addWidget(enableAudio_);
        
        QObject::connect(enableAudio_, &QCheckBox::stateChanged, this, &SpecialEffectsAssetsController::onSoundStateChanged);
        QObject::connect(enableAudio_, &QCheckBox::clicked, this, [this](bool checked) {
            reportEffectPickerSoundToggle(app_, EffectPickerPage::EffectAsset, checked);
        });
    }
    {
        tabWidget_ = new QTabWidget;
        applyDisabledTabStyle(tabWidget_);

        favoritesPage_ = new QWidget;
        favoritesStack_ = new QStackedLayout;
        favoritesPage_->setLayout(favoritesStack_);

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

        tabWidget_->addTab(favoritesPage_, tr("Favorites"));
        this->loadEffectList();
        vbox->addWidget(tabWidget_);
        QObject::connect(tabWidget_, &QTabWidget::currentChanged, this, &SpecialEffectsAssetsController::onCurrentChanged);
        auto* categoryTabBar = tabWidget_->findChild<QTabBar*>();
        assert(categoryTabBar != nullptr);
        QObject::connect(categoryTabBar, &QTabBar::tabBarClicked, this, [this](int index) {
            if (index < 0 || index >= tabWidget_->count()) {
                return;
            }
            auto* page = tabWidget_->widget(index);
            const uint32_t categoryId = page == favoritesPage_
                ? kFavoritesPageId
                : static_cast<SpCategoryWidget*>(page)->id();
            reportEffectPickerCategorySelect(app_,
                                             EffectPickerPage::EffectAsset,
                                             categoryId,
                                             to_u8(tabWidget_->tabText(index)));
        });
    }
    
    vbox->addStretch();
    
    this->setLayout(vbox);
}

SpecialEffectsAssetsController::~SpecialEffectsAssetsController() {
    auto* info = currentObsInfo(app_);
    if (info != nullptr) {
        info->unregisterObserver(this);
    }
    app_->unregisterFilterModelChangeObserver(this);
}

void SpecialEffectsAssetsController::switchLanguage() {
}

void SpecialEffectsAssetsController::onHVModeChanged(ObsInfo::HVMode mode) {
    for (auto& item : effectItems_) {
        if (item.favoriteWidget_ == nullptr) {
            continue;
        }

        const auto* effect = app_->effectCatalog()->effectInfo(item.effectId_);
        assert(effect != nullptr);
        const bool compatible = effect != nullptr && compatibleAtHVMode(effect->hv_, mode);
        item.favoriteWidget_->setVisible(compatible);
    }
}

void SpecialEffectsAssetsController::onFilterModelChanged(FilterModel* oldModel, FilterModel* newModel) {
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

void SpecialEffectsAssetsController::onSoundStateChanged(int s) {
    if (loadingConfig_) {
        return;
    }

    emit settingPatchRequested(JsonUtility::Patch::add(u8"/spEffects/enableSound", s == Qt::Checked));
}

void SpecialEffectsAssetsController::onCurrentChanged(int index) {
    if (loadingConfig_ || index < 0) {
        return;
    }

    auto* page = tabWidget_->widget(index);
    const auto selected = page == favoritesPage_
        ? kFavoritesPageId
        : static_cast<SpCategoryWidget*>(page)->id();
    emit settingPatchRequested(JsonUtility::Patch::add(u8"/spEffects/selected", selected));
}

void SpecialEffectsAssetsController::loadEffectList() {
    const auto& j = app_->effectCatalog()->specialEffectsJson();
    assert(!j.is_discarded());
    
    const auto& categories = j["categories"];
    assert(categories.is_array());
    
    for (auto i = 0; i < categories.size(); i++) {
        const auto& c = categories[i];
        uint32_t id = c["id"];
        QString name = I18nSupport::localizedName(c);
        auto* cc = new SpCategoryWidget(app_);
        cc->setId(id);
        const auto categoryIndex = static_cast<uint32_t>(i);
        QObject::connect(cc, &SpCategoryWidget::selectionChanged, this,
                         [this, cc, categoryIndex](uint32_t clickedEffectId,
                                                   uint32_t selectedEffectId) {
            syncFavoriteSelection(cc, selectedEffectId);
            if (loadingConfig_) {
                return;
            }
            emit settingPatchRequested(JsonUtility::Patch::add(spEffectSelectedPath(categoryIndex),
                                                         selectedEffectId));
            auto* item = effectItem(clickedEffectId);
            assert(item != nullptr);
            if (item != nullptr) {
                reportEffectPickerEffectSelect(
                    app_,
                    EffectPickerPage::EffectAsset,
                    cc->id(),
                    to_u8(tabWidget_->tabText(tabWidget_->indexOf(cc))),
                    std::nullopt,
                    {},
                    clickedEffectId,
                    to_u8(item->text_),
                    selectedEffectId == clickedEffectId,
                    tabWidget_->currentWidget() == favoritesPage_
                        ? u8"favorites"
                        : u8"category");
            }
        });
        QObject::connect(cc, &SpCategoryWidget::favoriteChanged, this,
                         [this](uint32_t effectId, bool favorite) {
            setEffectFavorite(effectId, favorite);
        });
        const auto& list = c["list"];
        for (const auto& item : list) {
            effectItems_.push_back({
                item["id"].get<uint32_t>(),
                I18nSupport::localizedName(item),
                from_json(item.value("iconpath", nlohmann::json())),
                cc,
                nullptr,
            });
        }

        cc->addEffects(list);
        categories_.push_back(cc);
        tabWidget_->addTab(cc, name);
    }

    reloadFavorites();
}

void SpecialEffectsAssetsController::setEffectFavorite(uint32_t effectId, bool favorite) {
    auto* item = effectItem(effectId);
    assert(item != nullptr);
    if (item == nullptr) {
        return;
    }

    reportEffectPickerFavoriteToggle(app_,
                                     EffectPickerPage::EffectAsset,
                                     effectId,
                                     to_u8(item->text_),
                                     favorite);

    if (!app_->settingConfig()->setFavorite(kFavoritePage, effectId, favorite)) {
        const bool current = item->favoriteWidget_ != nullptr;
        item->category_->setEffectFavorite(effectId, current);
        if (item->favoriteWidget_ != nullptr) {
            item->favoriteWidget_->setFavorite(current);
        }
        return;
    }

    applyEffectFavorite(*item, favorite);
}

void SpecialEffectsAssetsController::applyEffectFavorite(EffectItemViews& item, bool favorite) {
    item.category_->setEffectFavorite(item.effectId_, favorite);
    if (favorite == (item.favoriteWidget_ != nullptr)) {
        if (item.favoriteWidget_ != nullptr) {
            item.favoriteWidget_->setFavorite(favorite);
        }
        return;
    }

    if (favorite) {
        auto* widget = new EffectItemWidget(app_);
        widget->setId(item.effectId_);
        widget->setText(item.text_);
        widget->setImage(item.iconPath_);
        const auto* effect = app_->effectCatalog()->effectInfo(item.effectId_);
        assert(effect != nullptr);
        if (effect != nullptr) {
            widget->bindEffect(*effect);
        }
        widget->setFavorite(true);
        widget->setSelected(item.category_->selectedEffectId() == item.effectId_);
        QObject::connect(widget, &ImageTextWidget::clicked, this,
                         [category = item.category_, effectId = item.effectId_]() {
            category->selectEffect(effectId);
        });
        QObject::connect(widget, &ImageTextWidget::liked, this,
                         [this, effectId = item.effectId_](bool value) {
            setEffectFavorite(effectId, value);
        });

        item.favoriteWidget_ = widget;
        favoritesGridLayout_->addWidget(widget);
        widget->setVisible(isEffectCompatible(item.effectId_));
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

void SpecialEffectsAssetsController::reloadFavorites() {
    for (auto& item : effectItems_) {
        applyEffectFavorite(item,
                            app_->settingConfig()->isFavorite(kFavoritePage, item.effectId_));
    }
}

void SpecialEffectsAssetsController::syncFavoriteSelection(SpCategoryWidget* category,
                                                            uint32_t selectedEffectId) {
    for (auto& item : effectItems_) {
        if (item.category_ == category && item.favoriteWidget_ != nullptr) {
            item.favoriteWidget_->setSelected(item.effectId_ == selectedEffectId);
        }
    }
}

SpecialEffectsAssetsController::EffectItemViews*
SpecialEffectsAssetsController::effectItem(uint32_t effectId) {
    for (auto& item : effectItems_) {
        if (item.effectId_ == effectId) {
            return &item;
        }
    }

    return nullptr;
}

bool SpecialEffectsAssetsController::isEffectCompatible(uint32_t effectId) const {
    auto* info = currentObsInfo(app_);
    if (info == nullptr) {
        return true;
    }

    const auto* effect = app_->effectCatalog()->effectInfo(effectId);
    assert(effect != nullptr);
    return effect != nullptr && compatibleAtHVMode(effect->hv_, info->hvMode());
}

void SpecialEffectsAssetsController::reset() {
    resetState(true);
}

void SpecialEffectsAssetsController::resetState(bool resetCategories) {
    QSignalBlocker audioBlocker(enableAudio_);
    enableAudio_->setCheckState(Qt::Unchecked);

    if (resetCategories) {
        for (auto* category : categories_) {
            category->reset();
            syncFavoriteSelection(category, 0);
        }
    }
}

void SpecialEffectsAssetsController::json2Me(const nlohmann::json& j) {
    loadingConfig_ = true;

    const auto empty = nlohmann::json::object();
    const auto& source = j.is_object() ? j : empty;

    bool b = source.value("enableSound", false);
    {
        QSignalBlocker audioBlocker(enableAudio_);
        enableAudio_->setCheckState(b ? Qt::Checked : Qt::Unchecked);
    }

    uint32_t selected = source.value("selected", kFavoritesPageId);
    QWidget* selectedPage = favoritesPage_;
    for (auto* category : categories_) {
        if (selected == category->id()) {
            selectedPage = category;
            break;
        }
    }
    assert(selected == kFavoritesPageId || selectedPage != favoritesPage_);
    tabWidget_->setCurrentWidget(selectedPage);
    
    auto categoriesIt = source.find("categories");
    assert(categoriesIt != source.end() && categoriesIt->is_array());
    const auto categories = categoriesIt != source.end() ? *categoriesIt : nlohmann::json::array();
    for (auto* categoryWidget : categories_) {
        const nlohmann::json* matched = nullptr;
        for (auto i = 0; i < categories.size(); i++) {
            const auto& category = categories[i];
            uint32_t id = category.value("id", static_cast<uint32_t>(-1));
            if (id == categoryWidget->id()) {
                matched = &category;
                break;
            }
        }

        if (matched != nullptr) {
            categoryWidget->json2Me(*matched);
        } else {
            assert(false);
            nlohmann::json defaultCategory = nlohmann::json::object();
            defaultCategory["id"] = categoryWidget->id();
            defaultCategory["selected"] = 0;
            categoryWidget->json2Me(defaultCategory);
        }
        syncFavoriteSelection(categoryWidget, categoryWidget->selectedEffectId());
    }

    loadingConfig_ = false;
}

nlohmann::json SpecialEffectsAssetsController::me2Json() const {
    auto j = nlohmann::json::object();
    j["enableSound"] = (enableAudio_->checkState() == Qt::Checked);
    auto* page = tabWidget_->currentWidget();
    j["selected"] = page == favoritesPage_
        ? kFavoritesPageId
        : static_cast<SpCategoryWidget*>(page)->id();
    auto arr = nlohmann::json::array();
    for (auto* category : categories_) {
        arr.push_back(category->me2Json());
    }
    
    j["categories"].swap(arr);
    
    return j;
}
