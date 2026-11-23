// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include <algorithm>
#include <string_view>

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "nlohmann/json.hpp"
#include "categorizedcardgrid.h"
#include "commonui.h"
#include "effectitemwidget.h"
#include "filtermodelhelpers.h"
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

SpecialEffectsAssetsController::SpecialEffectsAssetsController(GlobalModel* app, QWidget* parent)
    : CiciWidget(parent), app_(app) {
    assert(app_ != nullptr);
    app_->registerFilterModelChangeObserver(this);
    if (auto* info = currentObsInfo(app_)) {
        info->registerObserver(this);
    }
}

SpecialEffectsAssetsController::~SpecialEffectsAssetsController() {
    if (auto* info = currentObsInfo(app_)) {
        info->unregisterObserver(this);
    }
    app_->unregisterFilterModelChangeObserver(this);
}

void SpecialEffectsAssetsController::createUI() {
    auto* title = new QHBoxLayout;
    title->addWidget(createLabel(tr("Camera:")));
    title->addWidget(createVideoInputLabel(app_, tr("No Camera")));
    title->addStretch();

    enableAudio_ = createCheckBox(tr("Turn on the sound"));
    connect(enableAudio_, &QCheckBox::stateChanged, this, &SpecialEffectsAssetsController::onSoundStateChanged);
    connect(enableAudio_, &QCheckBox::clicked, this, [this](bool checked) {
        reportEffectPickerSoundToggle(app_, EffectPickerPage::EffectAsset, checked);
    });

    cardGrid_ = new CategorizedCardGrid(this);
    loadEffectList();
    connect(cardGrid_, &CategorizedCardGrid::categoryActivated,
            this, &SpecialEffectsAssetsController::onCategoryActivated);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(title);
    layout->addWidget(enableAudio_);
    layout->addWidget(cardGrid_, 1);
}

void SpecialEffectsAssetsController::switchLanguage() {
}

void SpecialEffectsAssetsController::onHVModeChanged(ObsInfo::HVMode) {
    refreshCategories();
}

void SpecialEffectsAssetsController::onFilterModelChanged(FilterModel* oldModel, FilterModel* newModel) {
    if (oldModel != nullptr) {
        oldModel->obsInfo()->unregisterObserver(this);
    }
    if (newModel != nullptr) {
        newModel->obsInfo()->registerObserver(this);
    }
    refreshCategories();
}

void SpecialEffectsAssetsController::onSoundStateChanged(int state) {
    emit settingPatchRequested(JsonUtility::Patch::add(u8"/spEffects/enableSound", state == Qt::Checked));
}

void SpecialEffectsAssetsController::onCategoryActivated(const QString& view) {
    const auto id = view.toUInt();
    QString title = tr("Favorites");
    if (id != kFavoritesPageId) {
        const auto category = std::find_if(categories_.begin(), categories_.end(), [id](const auto& entry) {
            return entry.id_ == id;
        });
        assert(category != categories_.end());
        title = category->title_;
    }

    emit settingPatchRequested(JsonUtility::Patch::add(u8"/spEffects/selected", id));
    reportEffectPickerCategorySelect(app_, EffectPickerPage::EffectAsset, id, to_u8(title));
}

void SpecialEffectsAssetsController::loadEffectList() {
    const auto& j = app_->effectCatalog()->specialEffectsJson();
    assert(!j.is_discarded());

    for (const auto& entry : j["categories"]) {
        const auto categoryIndex = static_cast<uint32_t>(categories_.size());
        auto& category = categories_.emplace_back(Category{entry["id"], I18nSupport::localizedName(entry), {}});
        for (const auto& item : entry["list"]) {
            auto* card = new EffectItemWidget(app_, cardGrid_, ImageTextWidget::CardStyle::Wide);
            card->hide();
            card->setId(item["id"]);
            card->setText(I18nSupport::localizedName(item));
            card->setImage(from_json(item.value("iconpath", nlohmann::json())));
            const auto* effect = app_->effectCatalog()->effectInfo(card->id());
            assert(effect != nullptr);
            if (effect != nullptr) {
                card->bindEffect(*effect);
            }
            card->setFavorite(app_->settingConfig()->isFavorite(kFavoritePage, card->id()));
            connect(card, &ImageTextWidget::clicked, this, [this, categoryIndex, card] {
                selectEffect(categoryIndex, card);
            });
            connect(card, &ImageTextWidget::liked, this, [this, card](bool favorite) {
                setEffectFavorite(card, favorite);
            });
            category.cards_.append(card);
        }
    }
    refreshCategories();
}

void SpecialEffectsAssetsController::refreshCategories() {
    if (cardGrid_ == nullptr) {
        return;
    }

    QList<CategorizedCardGrid::Category> views{
        {QString::number(kFavoritesPageId), tr("Favorites"), tr("No favorites"), favoriteCards()},
    };
    for (const auto& category : categories_) {
        auto& view = views.emplaceBack(CategorizedCardGrid::Category{
            QString::number(category.id_), category.title_, {}, {}});
        for (auto* card : category.cards_) {
            if (isEffectCompatible(card->id())) {
                view.cards.append(card);
            }
        }
    }
    cardGrid_->setCategories(views);
}

QList<QWidget*> SpecialEffectsAssetsController::favoriteCards() const {
    QList<QWidget*> cards;
    for (const auto& category : categories_) {
        for (auto* card : category.cards_) {
            if (card->isFavorate() && isEffectCompatible(card->id())) {
                cards.append(card);
            }
        }
    }
    return cards;
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

void SpecialEffectsAssetsController::selectEffect(uint32_t categoryIndex, EffectItemWidget* card) {
    auto& category = categories_[categoryIndex];
    const bool selected = category.selected_ != card;
    const bool fromFavorites = cardGrid_->currentCategory().toUInt() == kFavoritesPageId;
    setSelectedEffect(category, selected ? card : nullptr);

    emit settingPatchRequested(JsonUtility::Patch::add(spEffectSelectedPath(categoryIndex),
                                                        selected ? card->id() : 0));
    reportEffectPickerEffectSelect(app_, EffectPickerPage::EffectAsset,
                                   category.id_, to_u8(category.title_), std::nullopt, {},
                                   card->id(), to_u8(card->text()), selected,
                                   fromFavorites ? u8"favorites" : u8"category");
}

void SpecialEffectsAssetsController::setSelectedEffect(Category& category, EffectItemWidget* card) {
    if (category.selected_ != nullptr) {
        category.selected_->setSelected(false);
    }
    category.selected_ = card;
    if (card != nullptr) {
        card->setSelected(true);
    }
}

void SpecialEffectsAssetsController::setEffectFavorite(EffectItemWidget* card, bool favorite) {
    reportEffectPickerFavoriteToggle(app_, EffectPickerPage::EffectAsset,
                                     card->id(), to_u8(card->text()), favorite);

    if (!app_->settingConfig()->setFavorite(kFavoritePage, card->id(), favorite)) {
        card->setFavorite(app_->settingConfig()->isFavorite(kFavoritePage, card->id()));
        return;
    }
    cardGrid_->setCategoryCards(QString::number(kFavoritesPageId), favoriteCards());
}

void SpecialEffectsAssetsController::reloadFavorites() {
    for (const auto& category : categories_) {
        for (auto* card : category.cards_) {
            card->setFavorite(app_->settingConfig()->isFavorite(kFavoritePage, card->id()));
        }
    }
    cardGrid_->setCategoryCards(QString::number(kFavoritesPageId), favoriteCards());
}

void SpecialEffectsAssetsController::reset() {
    const QSignalBlocker audioBlocker(enableAudio_);
    enableAudio_->setCheckState(Qt::Unchecked);
    for (auto& category : categories_) {
        setSelectedEffect(category, nullptr);
    }
}

void SpecialEffectsAssetsController::json2Me(const nlohmann::json& j) {
    const auto& source = j.is_object() ? j : JsonUtility::emptyObject();
    {
        const QSignalBlocker audioBlocker(enableAudio_);
        enableAudio_->setChecked(JsonUtility::boolAt(source, u8"enableSound"));
    }

    const auto selected = JsonUtility::uintAt(source, u8"selected", kFavoritesPageId);
    if (!cardGrid_->setCurrentCategory(QString::number(selected))) {
        cardGrid_->setCurrentCategory(QString::number(kFavoritesPageId));
    }

    const auto& savedCategories = JsonUtility::arrayAt(source, u8"categories");
    for (auto& category : categories_) {
        const auto saved = std::find_if(savedCategories.begin(), savedCategories.end(), [&category](const auto& entry) {
            return entry.is_object() && JsonUtility::uintAt(entry, u8"id") == category.id_;
        });
        const auto id = saved != savedCategories.end() ? JsonUtility::uintAt(*saved, u8"selected") : 0;
        const auto card = std::find_if(category.cards_.begin(), category.cards_.end(), [id](auto* item) {
            return item->id() == id;
        });
        setSelectedEffect(category, card != category.cards_.end() ? *card : nullptr);
    }
}

nlohmann::json SpecialEffectsAssetsController::me2Json() const {
    auto categories = nlohmann::json::array();
    for (const auto& category : categories_) {
        categories.push_back({
            {"id", category.id_},
            {"selected", category.selected_ != nullptr ? category.selected_->id() : 0},
        });
    }
    return {
        {"enableSound", enableAudio_->isChecked()},
        {"selected", cardGrid_->currentCategory().toUInt()},
        {"categories", std::move(categories)},
    };
}
