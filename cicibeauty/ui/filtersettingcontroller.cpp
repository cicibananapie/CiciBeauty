// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include <assert.h>
#include <algorithm>
#include <string>
#include <QLabel>
#include <QSlider>
#include <QLineEdit>
#include <QCheckBox>
#include <QSignalBlocker>
#include <QHBoxLayout>
#include <QSet>
#include <QVBoxLayout>

#include "nlohmann/json.hpp"
#include "assetsmgr.h"
#include "categorizedcardgrid.h"
#include "commonui.h"
#include "filtermodelhelpers.h"
#include "effectitemwidget.h"
#include "filtersettingcontroller.h"
#include "../effectcatalog.h"
#include "../i18nsupport.h"
#include "../qstringconv.h"
#include "../modelapp.h"
#include "../settingconfig.h"
#include "jsonutility.h"
#include "../statistics/settingspageanalytics.h"

namespace {
constexpr std::u8string_view kFavoritePage = u8"filter";
const QString kFavoritesCategory = QStringLiteral("favorites");

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
    
    currentFilter_ = new QLabel;
    currentFilter_->setStyleSheet(QStringLiteral(
        "QLabel:enabled { color: #58C7FF; }"
        "QLabel:disabled { color: #AEB5C2; }"));
    currentFilter_->setTextFormat(Qt::PlainText);
    currentFilter_->setWordWrap(true);
    currentFilter_->setMinimumWidth(120);
    currentFilter_->setFixedHeight(currentFilter_->fontMetrics().lineSpacing() * 2);

    auto* current = new QHBoxLayout;
    current->addWidget(createLabel(tr("Current filter")));
    current->addWidget(currentFilter_, 1);

    auto* strengthLabel = createLabel(tr("Strength"));
    strengthSlider_ = createSlider();
    strengthEditor_ = createEdit();
    strengthSlider_->setEnabled(false);
    strengthEditor_->setEnabled(false);
    strengthEditor_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    strengthLabel->setBuddy(strengthSlider_);
    strengthSlider_->setAccessibleName(tr("Strength"));
    strengthEditor_->setAccessibleName(tr("Strength"));

    strengthRow_ = new QWidget(this);
    auto* strength = new QHBoxLayout(strengthRow_);
    strength->setContentsMargins(0, 0, 0, 0);
    strength->addWidget(strengthLabel);
    strength->addWidget(strengthSlider_, 1);
    strength->addWidget(strengthEditor_);

    effectGrid_ = new CategorizedCardGrid(this);
    loadEffectList();
    
    QVBoxLayout* vbox = new QVBoxLayout(this);
    vbox->addLayout(titleHbox);
    vbox->addLayout(hbox1);
    vbox->addLayout(current);
    vbox->addWidget(strengthRow_);
    vbox->addWidget(effectGrid_, 1);
    
    QObject::connect(strengthSlider_, &QSlider::valueChanged, this, &FilterSettingController::onSliderValueChanged);
    QObject::connect(strengthEditor_, &QLineEdit::editingFinished, this, &FilterSettingController::onEditingFinished);
    QObject::connect(enableFilter_, &QCheckBox::stateChanged, this, &FilterSettingController::onEnableFilterStateChanged);
    QObject::connect(effectGrid_, &CategorizedCardGrid::categoryActivated, this, &FilterSettingController::onCategoryActivated);
    QObject::connect(enableFilter_, &QCheckBox::clicked, this, [this](bool checked) {
        reportFilterEnableToggle(app_, checked);
    });
    
    refreshEnableState();
}

FilterSettingController::~FilterSettingController() {
    auto* info = currentObsInfo(app_);
    if (info != nullptr) {
        info->unregisterObserver(this);
    }
    app_->unregisterFilterModelChangeObserver(this);
}

void FilterSettingController::onHVModeChanged(ObsInfo::HVMode) {
    refreshCategories();
}

void FilterSettingController::onFilterModelChanged(FilterModel* oldModel, FilterModel* newModel) {
    resetStrengthUi();
    if (oldModel != nullptr) {
        oldModel->obsInfo()->unregisterObserver(this);
    }
    if (newModel != nullptr) {
        newModel->obsInfo()->registerObserver(this);
    }

    refreshCategories();
}

void FilterSettingController::switchLanguage() {
    
}

void FilterSettingController::onSliderValueChanged(int value) {
    if (loadingConfig_ || !strengthSlider_->isEnabled() || selectedWidget_ == nullptr || !selectedWidget_->packageReady()) {
        return;
    }

    strengthEditor_->setText(QString::number(value));
    emit settingPatchRequested(JsonUtility::Patch::add(filterStrengthPath(selectedWidget_->id()), value));
}

void FilterSettingController::onEditingFinished() {
    if (loadingConfig_ || !strengthEditor_->isEnabled() || selectedWidget_ == nullptr || !selectedWidget_->packageReady()) {
        return;
    }

    auto* editor = strengthEditor_;
    const bool userEdited = editor->isModified();
    editor->setModified(false);
    bool ok = false;
    const int value = editor->text().toInt(&ok);
    if (!ok) {
        editor->setText(QString::number(strengthSlider_->value()));
        return;
    }
    if (strengthSlider_->value() != value) {
        QSignalBlocker blocker(strengthSlider_);
        strengthSlider_->setValue(value);
        editor->setText(QString::number(value));
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

void FilterSettingController::onCategoryActivated(const QString& id) {
    reportFilterViewSelect(app_, to_u8(id));
    emit settingPatchRequested(JsonUtility::Patch::add(u8"/filters/selectedTab", to_json(id)));
}

void FilterSettingController::refreshEnableState() {
    bool enableFilter = (enableFilter_->checkState() == Qt::Checked) ? true : false;
    effectGrid_->setEnabled(enableFilter);
    refreshParamEnabledState();
}

void FilterSettingController::refreshParamEnabledState() {
    const bool filterEnabled = enableFilter_->checkState() == Qt::Checked;
    strengthRow_->setEnabled(filterEnabled);

    currentFilter_->setEnabled(filterEnabled && selectedWidget_ != nullptr);
    if (selectedWidget_ == nullptr) {
        currentFilter_->setText(tr("No filter selected"));
    } else {
        const auto name = selectedWidget_->text();
        currentFilter_->setText(filterEnabled ? name : tr("%1 (disabled)").arg(name));
    }
}

void FilterSettingController::selectWidget(EffectItemWidget* newWidget) {
    assert(newWidget != nullptr);
    EffectItemWidget* selected = selectedWidget_ == newWidget ? nullptr : newWidget;
    auto complete = [this, newWidget, selected, source = to_u8(effectGrid_->currentCategory())](bool allowed) {
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
    }

    selectedWidget_ = newWidget;
    if (selectedWidget_ != nullptr) {
        selectedWidget_->setSelected(true);
    }
}

void FilterSettingController::restoreFromConfig(EffectItemWidget* newWidget) {
    setSelectedWidget(newWidget);
    refreshSelectedParamUi();
}

void FilterSettingController::refreshSelectedParamUi() {
    resetStrengthUi();
    if (selectedWidget_ == nullptr || !selectedWidget_->packageReady()) {
        return;
    }

    auto* model = app_->filterModel();
    if (model == nullptr) {
        return;
    }
    const auto effectId = selectedWidget_->id();
    const auto path = filterStrengthPath(effectId);
    model->queryEffectParamValues(
        paramRequests_, {path}, [this, path](const nlohmann::json& values) {
            const auto value = values.find(to_char(path));
            if (value == values.end() || value->get<int>() < 0) {
                return;
            }
            QSignalBlocker blocker(strengthSlider_);
            strengthSlider_->setValue(value->get<int>());
            strengthEditor_->setText(QString::number(strengthSlider_->value()));
            strengthSlider_->setEnabled(true);
            strengthEditor_->setEnabled(true);
        });
}

void FilterSettingController::resetStrengthUi() {
    paramRequests_.reset();
    if (strengthSlider_ != nullptr) {
        QSignalBlocker blocker(strengthSlider_);
        strengthSlider_->setEnabled(false);
        strengthEditor_->setEnabled(false);
        strengthSlider_->setValue(0);
        strengthEditor_->clear();
        refreshParamEnabledState();
    }
}

void FilterSettingController::setEffectFavorite(EffectItemWidget* widget, bool favorite) {
    if (!app_->settingConfig()->setFavorite(kFavoritePage, widget->id(), favorite)) {
        widget->setFavorite(app_->settingConfig()->isFavorite(kFavoritePage, widget->id()));
        return;
    }

    widget->setFavorite(favorite);
    effectGrid_->setCategoryCards(kFavoritesCategory, favoriteCards());
}

void FilterSettingController::reloadFavorites() {
    for (const auto& item : effectItems_) {
        item.widget_->setFavorite(app_->settingConfig()->isFavorite(kFavoritePage, item.widget_->id()));
    }
    effectGrid_->setCategoryCards(kFavoritesCategory, favoriteCards());
}

QList<QWidget*> FilterSettingController::favoriteCards() const {
    QList<QWidget*> cards;
    for (const auto& item : effectItems_) {
        if (item.widget_->isFavorate() && isEffectCompatible(item.widget_->id())) {
            cards.append(item.widget_);
        }
    }
    return cards;
}

void FilterSettingController::loadEffectList() {
    const auto& j = app_->effectCatalog()->filtersJson();
    assert(!j.is_discarded());
    
    const auto& list = j["list"];
    for (auto i = 0; i < list.size(); i++) {
        const auto& item = list[i];
        auto* w = new EffectItemWidget(app_, effectGrid_, ImageTextWidget::CardStyle::Wide);
        w->hide();
        w->setId(item["id"]);
        w->setText(I18nSupport::localizedName(item));
        w->setImage(from_json(item.value("iconpath", nlohmann::json())));
        const auto* effect = app_->effectCatalog()->effectInfo(w->id());
        assert(effect != nullptr);
        w->bindEffect(*effect);
        w->setFavorite(app_->settingConfig()->isFavorite(kFavoritePage, w->id()));
        
        auto connection = QObject::connect(w, &ImageTextWidget::clicked, this, [this, w]() {
            selectWidget(w);
        });
        assert(connection);
        QObject::connect(w, &ImageTextWidget::liked, this, [this, w](bool favorite) {
            reportFilterFavoriteToggle(app_,
                                       w->id(),
                                       to_u8(w->text()),
                                       favorite);
            setEffectFavorite(w, favorite);
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
        effectItems_.append({w, from_json(item.value("category", nlohmann::json()))});
    }

    refreshCategories();
    effectGrid_->setCurrentCategory(defaultCategory_);
}

void FilterSettingController::refreshCategories() {
    if (effectGrid_ == nullptr) {
        return;
    }

    const auto emptyText = tr("No filters in this category");
    QList<CategorizedCardGrid::Category> categories{
        {kFavoritesCategory, tr("Favorites"), tr("No favorites"), favoriteCards()},
        {QStringLiteral("natural"), tr("Natural"), emptyText, {}},
        {QStringLiteral("bright"), tr("Fresh"), emptyText, {}},
        {QStringLiteral("film"), tr("Film"), emptyText, {}},
        {QStringLiteral("moody"), tr("Moody"), emptyText, {}},
        {QStringLiteral("monochrome"), tr("B/W"), emptyText, {}},
        {QStringLiteral("uncategorized"), tr("Uncategorized"), emptyText, {}},
    };

    QSet<QString> populated;
    for (const auto& item : effectItems_) {
        auto category = std::find_if(categories.begin() + 1, categories.end(), [&item](const auto& entry) {
            return entry.id == item.category_;
        });
        if (category == categories.end()) {
            category = categories.end() - 1;
        }
        populated.insert(category->id);
        if (isEffectCompatible(item.widget_->id())) {
            category->cards.append(item.widget_);
        }
    }
    for (auto index = categories.size() - 1; index > 0; --index) {
        if (!populated.contains(categories.at(index).id)) {
            categories.removeAt(index);
        }
    }

    defaultCategory_ = categories.size() > 1 ? categories.at(1).id : kFavoritesCategory;
    effectGrid_->setCategories(categories);
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

    const auto savedTab = j.find("selectedTab");
    const auto category = savedTab != j.end() && savedTab->is_string()
        ? from_json(*savedTab)
        : (uintAt(j, u8"selectedTab", 1) == 0 ? kFavoritesCategory : defaultCategory_);
    if (!effectGrid_->setCurrentCategory(category)) {
        effectGrid_->setCurrentCategory(defaultCategory_);
    }

    const auto selected = uintAt(j, u8"selected");
    for (const auto& item : effectItems_) {
        if (selected == item.widget_->id()) {
            restoreFromConfig(item.widget_);
            break;
        }
    }

    loadingConfig_ = false;
}

void FilterSettingController::reset() {
    QSignalBlocker blocker(enableFilter_);
    enableFilter_->setCheckState(Qt::Unchecked);

    restoreFromConfig(nullptr);

    refreshEnableState();
}

bool FilterSettingController::isEffectCompatible(uint64_t effectId) const {
    const auto* info = currentObsInfo(app_);
    if (info == nullptr) {
        return true;
    }

    const auto* effect = app_->effectCatalog()->effectInfo(effectId);
    assert(effect != nullptr);
    return compatibleAtHVMode(effect->hv_, info->hvMode());
}
