// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include <assert.h>
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
#include "imagetextwidget.h"
#include "gridmorelayout.h"
#include "aieffectcontroller.h"
#include "../effectcatalog.h"
#include "../i18nsupport.h"
#include "../qstringconv.h"
#include "../modelapp.h"
#include "../settingconfig.h"
#include "jsonutility.h"
#include "../statistics/settingspageanalytics.h"

namespace {

constexpr uint32_t kFavoriteCategoryId = 0;
constexpr std::u8string_view kFavoritePage = u8"aiEffects";
constexpr int kActionGroupIconSize = 64;

std::u8string aiActionSelectedPath(uint32_t categoryIndex, uint32_t actionGroupIndex) {
    return std::u8string(u8"/aiEffects/categories/")
         + to_u8(categoryIndex)
         + u8"/actionGroups/"
         + to_u8(actionGroupIndex)
         + u8"/selected";
}

}

ActionGroupWidget::ActionGroupWidget(GlobalModel *app, QWidget *parent /* = nullptr*/) : CiciWidget(parent), app_(app) {
    assert(app_ != nullptr);
}

ActionGroupWidget::~ActionGroupWidget() {
}

void ActionGroupWidget::onItemClicked(EffectItemWidget* w) {
    assert(w != nullptr);
	if (selectedWidget_ == w) {
		selectedWidget_->setSelected(false);
        selectedWidget_ = nullptr;
    }
    else {
	    if (selectedWidget_ != nullptr) {
		    selectedWidget_->setSelected(false);
	    }
        selectedWidget_ = w;
        selectedWidget_->setSelected(true);
    }

    emit selectionChanged(w->id(), selectedWidget_ != nullptr ? selectedWidget_->id() : 0);
}

void ActionGroupWidget::selectEffect(uint32_t effectId) {
    for (auto* widget : widgets_) {
        if (widget->id() == effectId) {
            onItemClicked(widget);
            return;
        }
    }

    assert(false);
}

void ActionGroupWidget::setEffectFavorite(uint32_t effectId, bool favorite) {
    for (auto* widget : widgets_) {
        if (widget->id() == effectId) {
            widget->setFavorite(favorite);
            return;
        }
    }

    assert(false);
}

uint32_t ActionGroupWidget::selectedEffectId() const {
    return selectedWidget_ != nullptr ? selectedWidget_->id() : 0;
}

QString ActionGroupWidget::name() const {
    return actionType_ != nullptr ? actionType_->text() : QString();
}

void ActionGroupWidget::addActions(const nlohmann::json& j) {
    QHBoxLayout* hbox = new QHBoxLayout;
    {
        QString name = I18nSupport::localizedName(j);
        QString icon = from_json(j.value("iconpath", nlohmann::json()));
        QVBoxLayout* vbox = new QVBoxLayout;
        actionType_ = new ImageTextWidget(app_->assets());
        actionType_->setImageDisplaySize(QSize(kActionGroupIconSize, kActionGroupIconSize));
        actionType_->setText(name);
        actionType_->setImage(icon);
        actionType_->setFavoriteEnabled(false);
        actionType_->setClickableEnabled(false);
        actionType_->setHoverEffectEnabled(false);
        vbox->addWidget(actionType_);
        vbox->addStretch();
        hbox->addItem(vbox);
        
        uint32_t id = j["id"];
        actionType_->setId(id);
        this->setId(id);
    }
    QWidget* actionsWidget = new QWidget;
    {
        const auto& actions = j["actions"];
        assert(actions.is_array());
        GridMoreLayout* grid = new GridMoreLayout;
        for (auto i = 0; i < actions.size(); i++) {
            const auto& ac = actions[i];
            uint32_t id = ac["id"];
            QString name = I18nSupport::localizedName(ac);
            QString icon = from_json(ac.value("iconpath", nlohmann::json()));
            auto* w = new EffectItemWidget(app_);
            w->setId(id);
            w->setText(name);
            w->setImage(icon);
            const auto* effect = app_->effectCatalog()->effectInfo(id);
            assert(effect != nullptr);
            if (effect != nullptr) {
                w->bindEffect(*effect);
            }
            grid->addWidget(w);
            widgets_.push_back(w);
            
            auto connection = QObject::connect(w, &ImageTextWidget::clicked, this, [this, w]() {
                onItemClicked(w);
            });
            assert(connection);
            QObject::connect(w, &ImageTextWidget::liked, this, [this, w](bool favorite) {
                emit favoriteChanged(w->id(), favorite);
            });
            
            (void)id;
        }
        actionsWidget->setLayout(grid);
    }
    hbox->addWidget(actionsWidget);
    this->setLayout(hbox);
}

void ActionGroupWidget::reset() {
    if (selectedWidget_ != nullptr) {
        selectedWidget_->setSelected(false);
        selectedWidget_ = nullptr;
    }
    for (auto* w : widgets_) {
        w->setSelected(false);
    }
}

void ActionGroupWidget::json2Me(const nlohmann::json& j) {
    reset();

    const auto empty = nlohmann::json::object();
    const auto& source = j.is_object() ? j : empty;
    if (source.value("id", id()) != id()) {
        assert(false);
    }

    uint32_t selected = source.value("selected", 0);
    for (auto i = 0; i < widgets_.size(); i++) {
        auto w = widgets_[i];
        if (w == nullptr) {
            continue;
        }
        
        if (selected == w->id()) {
            selectedWidget_ = w;
            selectedWidget_->setSelected(true);
            break;
        }
    }
}

nlohmann::json ActionGroupWidget::me2Json() const {
    assert(id() == actionType_->id());
    nlohmann::json j;
    j["id"] = id();
    j["selected"] = selectedWidget_ != nullptr ? selectedWidget_->id() : 0;
    
    return j;
}

CategoryWidget::CategoryWidget(GlobalModel* app, QWidget* parent/* = nullptr*/)
: CiciWidget(parent), app_(app) {
    
}

CategoryWidget::~CategoryWidget() {
    
}

void CategoryWidget::addActionGroups(const nlohmann::json& category) {
    uint32_t id = category["id"];
    QVBoxLayout* vbox = new QVBoxLayout;
    this->setId(id);
    const auto& actionGroups = category["actionGroups"];
    assert(actionGroups.is_array());
    for (auto i = 0; i < actionGroups.size(); i++) {
        ActionGroupWidget* action = new ActionGroupWidget(app_);
        action->addActions(actionGroups[i]);
        const auto actionGroupIndex = static_cast<uint32_t>(i);
        QObject::connect(action,
                         &ActionGroupWidget::selectionChanged,
                         this,
                         [this, action, actionGroupIndex](uint32_t clickedEffectId,
                                                         uint32_t selectedEffectId) {
                             emit actionGroupSelectionChanged(actionGroupIndex,
                                                              action->id(),
                                                              clickedEffectId,
                                                              selectedEffectId);
                         });
        QObject::connect(action,
                         &ActionGroupWidget::favoriteChanged,
                         this,
                         [this, action, actionGroupIndex](uint32_t effectId, bool favorite) {
                             emit actionGroupFavoriteChanged(actionGroupIndex,
                                                             action->id(),
                                                             effectId,
                                                             favorite);
                         });
        vbox->addWidget(action);
        if (i != actionGroups.size() - 1) {
            vbox->addWidget(createHSeperator());
        }
        widgets_.push_back(action);
    }
    vbox->addStretch(1);
    //vbox->addItem(createExpandingVerticalSpacer());
    addScrollBar(vbox);
}

ActionGroupWidget* CategoryWidget::actionGroupAt(uint32_t index) const {
    if (index >= widgets_.size()) {
        assert(false);
        return nullptr;
    }

    return widgets_[index];
}

void CategoryWidget::reset() {
    for (auto* w : widgets_) {
        w->reset();
    }
}

void CategoryWidget::json2Me(const nlohmann::json& j) {
    const auto empty = nlohmann::json::object();
    const auto& source = j.is_object() ? j : empty;
    if (source.value("id", id()) != id()) {
        assert(false);
    }

    auto arrIt = source.find("actionGroups");
    assert(arrIt != source.end() && arrIt->is_array());
    const auto arr = arrIt != source.end() ? *arrIt : nlohmann::json::array();

    for (auto* w : widgets_) {
        const nlohmann::json* matched = nullptr;
        for (auto i = 0; i < arr.size(); i++) {
            const auto& ag = arr[i];
            uint32_t id = ag.value("id", static_cast<uint32_t>(-1));
            if (id == w->id()) {
                matched = &ag;
                break;
            }
        }

        if (matched != nullptr) {
            w->json2Me(*matched);
        } else {
            assert(false);
            nlohmann::json defaultActionGroup = nlohmann::json::object();
            defaultActionGroup["id"] = w->id();
            defaultActionGroup["selected"] = 0;
            w->json2Me(defaultActionGroup);
        }
    }
}

nlohmann::json CategoryWidget::me2Json() const {
    if (widgets_.empty()) {
        return nlohmann::json();
    }

    auto arr = nlohmann::json::array();
    for (auto i = 0; i < widgets_.size(); i++) {
        auto ag = widgets_[i];
        arr.push_back(ag->me2Json());
    }
    
    auto j = nlohmann::json::object();
    j["id"] = id();
    
    j["actionGroups"].swap(arr);
    
    return j;
}

AIEffectController::AIEffectController(GlobalModel* app, QWidget* parent/* = nullptr*/)
: CiciWidget(parent), app_(app) {
}

void AIEffectController::createUI() {
    QVBoxLayout *vbox = new QVBoxLayout;
    vbox->setContentsMargins(0, 0, 0, 0);
    {
        QHBoxLayout* hbox = new QHBoxLayout;
        QLabel* cameraLabel = createLabel(tr("Camera:"));
        QLabel* cameraNameLabel = createVideoInputLabel(app_, tr("No Camera"));
        hbox->addWidget(cameraLabel);
        hbox->addWidget(cameraNameLabel);
        hbox->addStretch();
        vbox->addItem(hbox);
    }
    {
        enableAudio_ = createCheckBox(tr("Turn on the sound"));
        vbox->addWidget(enableAudio_);
            
        QObject::connect(enableAudio_, &QCheckBox::stateChanged, this, &AIEffectController::onSoundStateChanged);
        QObject::connect(enableAudio_, &QCheckBox::clicked, this, [this](bool checked) {
            reportEffectPickerSoundToggle(app_, EffectPickerPage::AiEffect, checked);
        });
    }
    {
        tabWidget_ = new QTabWidget;
        applyDisabledTabStyle(tabWidget_);

        favoritesPage_ = new QWidget;
        favoritesStack_ = new QStackedLayout;
        favoritesPage_->setLayout(favoritesStack_);

        auto* favoritesListPage = new CiciWidget;
        favoritesGroupsLayout_ = new QVBoxLayout;
        favoritesListPage->addScrollBar(favoritesGroupsLayout_);
        favoritesStack_->addWidget(favoritesListPage);

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
        
        loadEffectList();
        vbox->addWidget(tabWidget_);
        QObject::connect(tabWidget_, &QTabWidget::currentChanged, this, &AIEffectController::onCurrentChanged);
        auto* categoryTabBar = tabWidget_->findChild<QTabBar*>();
        assert(categoryTabBar != nullptr);
        QObject::connect(categoryTabBar, &QTabBar::tabBarClicked, this, [this](int index) {
            if (index < 0 || index >= tabWidget_->count()) {
                return;
            }
            auto* page = tabWidget_->widget(index);
            const uint32_t categoryId = page == favoritesPage_
                ? kFavoriteCategoryId
                : static_cast<CategoryWidget*>(page)->id();
            reportEffectPickerCategorySelect(app_,
                                             EffectPickerPage::AiEffect,
                                             categoryId,
                                             to_u8(tabWidget_->tabText(index)));
        });
    }
    vbox->addStretch();

    auto* content = new QWidget;
    content->setLayout(vbox);
    content->setEnabled(false);

    auto* notice = new QLabel(tr("Gesture effects are coming soon and are not available in this version."));
    notice->setAlignment(Qt::AlignCenter);
    notice->setWordWrap(true);
    notice->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    notice->setStyleSheet(QStringLiteral(
        "color: #F3D38A;"
        "background-color: #332D1F;"
        "border: 1px solid #6B5830;"
        "border-radius: 4px;"
        "font-weight: 500;"
        "padding: 8px;"));

    auto* pageLayout = new QVBoxLayout;
    pageLayout->addWidget(notice);
    pageLayout->addWidget(content, 1);
    this->setLayout(pageLayout);
}

AIEffectController::~AIEffectController() {
    
}

void AIEffectController::onSoundStateChanged(int s) {
    if (loadingConfig_) {
        return;
    }

    emit settingPatchRequested(JsonUtility::Patch::add(u8"/aiEffects/enableSound", s == Qt::Checked));
}

void AIEffectController::onCurrentChanged(int index) {
    if (loadingConfig_ || index < 0) {
        return;
    }

    auto* page = tabWidget_->widget(index);
    const auto selected = page == favoritesPage_
        ? kFavoriteCategoryId
        : static_cast<CategoryWidget*>(page)->id();
    emit settingPatchRequested(JsonUtility::Patch::add(u8"/aiEffects/selected", selected));
}

void AIEffectController::loadEffectList() {
    const auto& j = app_->effectCatalog()->aiEffectsJson();
    assert(!j.is_discarded());
    
    const auto& categories = j["categories"];
    assert(categories.is_array());
    for (auto i = 0; i < categories.size(); i++) {
        const auto& category = categories[i];
        assert(category.is_object());
        uint32_t id = category["id"];
        QString name = I18nSupport::localizedName(category);
        CategoryWidget* tab = new CategoryWidget(app_);
        tab->setId(id);
        tab->addActionGroups(category);
        const auto categoryIndex = static_cast<uint32_t>(i);
        QObject::connect(tab,
                         &CategoryWidget::actionGroupSelectionChanged,
                         this,
                         [this, tab, categoryIndex](uint32_t actionGroupIndex,
                                                    uint32_t actionGroupId,
                                                    uint32_t clickedEffectId,
                                                    uint32_t selectedEffectId) {
                             syncFavoriteSelection(tab->actionGroupAt(actionGroupIndex),
                                                   selectedEffectId);
                             if (loadingConfig_) {
                                 return;
                             }
                             emit settingPatchRequested(JsonUtility::Patch::add(
                                 aiActionSelectedPath(categoryIndex, actionGroupIndex),
                                 selectedEffectId));
                             auto* item = effectItem(clickedEffectId);
                             assert(item != nullptr);
                             auto* group = tab->actionGroupAt(actionGroupIndex);
                             assert(group != nullptr);
                             if (item != nullptr && group != nullptr) {
                                 reportEffectPickerEffectSelect(
                                     app_,
                                     EffectPickerPage::AiEffect,
                                     tab->id(),
                                     to_u8(tabWidget_->tabText(tabWidget_->indexOf(tab))),
                                     actionGroupId,
                                     to_u8(group->name()),
                                     clickedEffectId,
                                     to_u8(item->text_),
                                     selectedEffectId == clickedEffectId,
                                     tabWidget_->currentWidget() == favoritesPage_
                                         ? u8"favorites"
                                         : u8"category");
                             }
                         });
        QObject::connect(tab,
                         &CategoryWidget::actionGroupFavoriteChanged,
                         this,
                         [this](uint32_t, uint32_t, uint32_t effectId, bool favorite) {
                             setEffectFavorite(effectId, favorite);
                         });

        const auto& actionGroups = category["actionGroups"];
        assert(actionGroups.is_array());
        for (auto actionGroupIndex = 0; actionGroupIndex < actionGroups.size();
             ++actionGroupIndex) {
            const auto& actionGroup = actionGroups[actionGroupIndex];
            auto* originalGroup = tab->actionGroupAt(static_cast<uint32_t>(actionGroupIndex));
            assert(originalGroup != nullptr);
            if (originalGroup == nullptr) {
                continue;
            }

            auto* rowWidget = new QWidget;
            auto* rowLayout = new QHBoxLayout;
            auto* groupInfoLayout = new QVBoxLayout;
            auto* groupInfo = new ImageTextWidget(app_->assets());
            groupInfo->setImageDisplaySize(QSize(kActionGroupIconSize, kActionGroupIconSize));
            groupInfo->setId(actionGroup["id"]);
            groupInfo->setText(I18nSupport::localizedName(actionGroup));
            groupInfo->setImage(from_json(actionGroup.value("iconpath", nlohmann::json())));
            groupInfo->setFavoriteEnabled(false);
            groupInfo->setClickableEnabled(false);
            groupInfo->setHoverEffectEnabled(false);
            groupInfoLayout->addWidget(groupInfo);
            groupInfoLayout->addStretch();
            rowLayout->addItem(groupInfoLayout);

            auto* actionsWidget = new QWidget;
            auto* favoritesGrid = new GridMoreLayout;
            actionsWidget->setLayout(favoritesGrid);
            rowLayout->addWidget(actionsWidget);
            rowWidget->setLayout(rowLayout);
            rowWidget->hide();
            favoritesGroupsLayout_->addWidget(rowWidget);

            const auto favoriteGroupIndex = static_cast<uint32_t>(favoriteGroups_.size());
            favoriteGroups_.push_back({originalGroup, rowWidget, favoritesGrid, 0});

            const auto& actions = actionGroup["actions"];
            assert(actions.is_array());
            for (const auto& action : actions) {
                const auto effectId = action["id"].get<uint32_t>();
                assert(effectItem(effectId) == nullptr);
                effectItems_.push_back({
                    effectId,
                    I18nSupport::localizedName(action),
                    from_json(action.value("iconpath", nlohmann::json())),
                    originalGroup,
                    favoriteGroupIndex,
                    nullptr,
                });
            }
        }

        categories_.push_back(tab);
        tabWidget_->addTab(tab, name);
    }

    favoritesGroupsLayout_->addStretch(1);
    reloadFavorites();
}

void AIEffectController::setEffectFavorite(uint32_t effectId, bool favorite) {
    auto* item = effectItem(effectId);
    assert(item != nullptr);
    if (item == nullptr) {
        return;
    }

    reportEffectPickerFavoriteToggle(app_,
                                     EffectPickerPage::AiEffect,
                                     effectId,
                                     to_u8(item->text_),
                                     favorite);

    if (!app_->settingConfig()->setFavorite(kFavoritePage, effectId, favorite)) {
        const bool current = item->favoriteWidget_ != nullptr;
        item->originalGroup_->setEffectFavorite(effectId, current);
        if (item->favoriteWidget_ != nullptr) {
            item->favoriteWidget_->setFavorite(current);
        }
        return;
    }

    applyEffectFavorite(*item, favorite);
}

void AIEffectController::applyEffectFavorite(EffectItemViews& item, bool favorite) {
    assert(item.favoriteGroupIndex_ < favoriteGroups_.size());
    if (item.favoriteGroupIndex_ >= favoriteGroups_.size()) {
        return;
    }

    auto& group = favoriteGroups_[item.favoriteGroupIndex_];
    item.originalGroup_->setEffectFavorite(item.effectId_, favorite);
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
        widget->setSelected(item.originalGroup_->selectedEffectId() == item.effectId_);
        QObject::connect(widget, &ImageTextWidget::clicked, this,
                         [originalGroup = item.originalGroup_, effectId = item.effectId_]() {
            originalGroup->selectEffect(effectId);
        });
        QObject::connect(widget, &ImageTextWidget::liked, this,
                         [this, effectId = item.effectId_](bool value) {
            setEffectFavorite(effectId, value);
        });

        item.favoriteWidget_ = widget;
        group.grid_->addWidget(widget);
        ++group.favoriteCount_;
        group.rowWidget_->show();
        ++favoriteCount_;
        favoritesStack_->setCurrentIndex(0);
        return;
    }

    auto* widget = item.favoriteWidget_;
    item.favoriteWidget_ = nullptr;
    widget->hide();
    group.grid_->removeWidget(widget);
    widget->deleteLater();

    --group.favoriteCount_;
    assert(group.favoriteCount_ >= 0);
    if (group.favoriteCount_ == 0) {
        group.rowWidget_->hide();
    }

    --favoriteCount_;
    assert(favoriteCount_ >= 0);
    if (favoriteCount_ == 0) {
        favoritesStack_->setCurrentIndex(1);
    }
}

void AIEffectController::reloadFavorites() {
    for (auto& item : effectItems_) {
        applyEffectFavorite(item,
                            app_->settingConfig()->isFavorite(kFavoritePage, item.effectId_));
    }
}

void AIEffectController::syncFavoriteSelection(ActionGroupWidget* group,
                                               uint32_t selectedEffectId) {
    if (group == nullptr) {
        return;
    }

    for (auto& item : effectItems_) {
        if (item.originalGroup_ == group && item.favoriteWidget_ != nullptr) {
            item.favoriteWidget_->setSelected(item.effectId_ == selectedEffectId);
        }
    }
}

void AIEffectController::syncAllFavoriteSelections() {
    for (const auto& group : favoriteGroups_) {
        syncFavoriteSelection(group.originalGroup_, group.originalGroup_->selectedEffectId());
    }
}

AIEffectController::EffectItemViews* AIEffectController::effectItem(uint32_t effectId) {
    for (auto& item : effectItems_) {
        if (item.effectId_ == effectId) {
            return &item;
        }
    }

    return nullptr;
}

void AIEffectController::reset() {
    resetState(true);
}

void AIEffectController::resetState(bool resetCategories) {
    QSignalBlocker audioBlocker(enableAudio_);
    enableAudio_->setCheckState(Qt::Unchecked);

    QSignalBlocker tabBlocker(tabWidget_);
    tabWidget_->setCurrentIndex(0);

    if (resetCategories) {
        for (auto* category : categories_) {
            category->reset();
        }
        syncAllFavoriteSelections();
    }
}

void AIEffectController::json2Me(const nlohmann::json& j) {
    loadingConfig_ = true;
    resetState(false);

    const auto empty = nlohmann::json::object();
    const auto& source = j.is_object() ? j : empty;

    bool b = source.value("enableSound", false);
    enableAudio_->setCheckState(b?Qt::Checked:Qt::Unchecked);
    uint32_t selected = source.value("selected", kFavoriteCategoryId);
    QWidget* selectedPage = favoritesPage_;
    for (auto* category : categories_) {
        if (selected == category->id()) {
            selectedPage = category;
            break;
        }
    }
    tabWidget_->setCurrentWidget(selectedPage);
    
    auto arrIt = source.find("categories");
    assert(arrIt != source.end() && arrIt->is_array());
    const auto arr = arrIt != source.end() ? *arrIt : nlohmann::json::array();
    for (auto* categoryWidget : categories_) {
        const nlohmann::json* matched = nullptr;
        for (auto i = 0; i < arr.size(); i++) {
            const auto& item = arr[i];
            uint32_t id = item.value("id", static_cast<uint32_t>(-1));
            if (id == categoryWidget->id()) {
                matched = &item;
                break;
            }
        }

        assert(matched != nullptr);
        if (matched == nullptr) {
            continue;
        }
        categoryWidget->json2Me(*matched);
    }

    syncAllFavoriteSelections();
    loadingConfig_ = false;
}

nlohmann::json AIEffectController::me2Json() const {
    auto j = nlohmann::json::object();
    j["enableSound"] = (enableAudio_->checkState() == Qt::Checked);
    auto* page = tabWidget_->currentWidget();
    j["selected"] = page == favoritesPage_
        ? kFavoriteCategoryId
        : static_cast<CategoryWidget*>(page)->id();
    auto arr = nlohmann::json::array();
    for (auto* category : categories_) {
        auto item = category->me2Json();
        if (!item.is_null()) {
            arr.push_back(item);
        }
    }
    j["categories"].swap(arr);
    
    return j;
}
