// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <stdint.h>
#include <QString>
#include <vector>
#include <string>

#include "ciciwidget.h"
#include "../obsinfo.h"
#include "../modelapp.h"

class ImageTextWidget;
class EffectItemWidget;
class GridMoreLayout;
class QStackedLayout;
class QTabWidget;
class QCheckBox;
class QVBoxLayout;

class GlobalModel;
class ActionGroupWidget: public CiciWidget {
    Q_OBJECT
    
public:
    ActionGroupWidget(GlobalModel *app, QWidget *parent = nullptr);
    ~ActionGroupWidget();
    
public:
    void addActions(const nlohmann::json& j);
    void selectEffect(uint32_t effectId);
    void setEffectFavorite(uint32_t effectId, bool favorite);
    uint32_t selectedEffectId() const;
    QString name() const;
    
public:
    void json2Me(const nlohmann::json& j) override;
    void reset() override;
    nlohmann::json me2Json() const;

signals:
    void selectionChanged(uint32_t clickedEffectId, uint32_t selectedEffectId);
    void favoriteChanged(uint32_t effectId, bool favorite);

private:
    void onItemClicked(EffectItemWidget* w);

private:
    ImageTextWidget* actionType_ = nullptr;
    EffectItemWidget* selectedWidget_ = nullptr;
    std::vector<EffectItemWidget*> widgets_;
    GlobalModel *app_ = nullptr;
};

class CategoryWidget: public CiciWidget {
    Q_OBJECT
    
public:
    CategoryWidget(GlobalModel *app, QWidget *parent = nullptr);
    ~CategoryWidget();
    
public:
    void addActionGroups(const nlohmann::json& j);
    ActionGroupWidget* actionGroupAt(uint32_t index) const;
    
public:
    void json2Me(const nlohmann::json& j) override;
    void reset() override;
    nlohmann::json me2Json() const;

signals:
    void actionGroupSelectionChanged(uint32_t actionGroupIndex,
                                     uint32_t actionGroupId,
                                     uint32_t clickedEffectId,
                                     uint32_t selectedEffectId);
    void actionGroupFavoriteChanged(uint32_t actionGroupIndex,
                                    uint32_t actionGroupId,
                                    uint32_t effectId,
                                    bool favorite);
    
private:
    std::vector<ActionGroupWidget*> widgets_;
	GlobalModel *app_ = nullptr;
};

class AIEffectController: public CiciWidget {
    Q_OBJECT

public:
    AIEffectController(GlobalModel *app, QWidget *parent = nullptr);
    ~AIEffectController();
    
public:
    void createUI();
    void reloadFavorites();
    
public:
    void reset() override;
    void json2Me(const nlohmann::json& j) override;
    nlohmann::json me2Json() const;

signals:
    void settingPatchRequested(const nlohmann::json& patch);
    
private slots:
    void onSoundStateChanged(int);
    void onCurrentChanged(int);
    
private:
    struct FavoriteGroupView {
        ActionGroupWidget* originalGroup_ = nullptr;
        QWidget* rowWidget_ = nullptr;
        GridMoreLayout* grid_ = nullptr;
        int favoriteCount_ = 0;
    };

    struct EffectItemViews {
        uint32_t effectId_ = 0;
        QString text_;
        QString iconPath_;
        ActionGroupWidget* originalGroup_ = nullptr;
        uint32_t favoriteGroupIndex_ = 0;
        EffectItemWidget* favoriteWidget_ = nullptr;
    };

    void loadEffectList();
    void resetState(bool resetCategories);
    void setEffectFavorite(uint32_t effectId, bool favorite);
    void applyEffectFavorite(EffectItemViews& item, bool favorite);
    void syncFavoriteSelection(ActionGroupWidget* group, uint32_t selectedEffectId);
    void syncAllFavoriteSelections();
    EffectItemViews* effectItem(uint32_t effectId);
    
private:
    QTabWidget* tabWidget_ = nullptr;
    QCheckBox* enableAudio_ = nullptr;
    QWidget* favoritesPage_ = nullptr;
    QStackedLayout* favoritesStack_ = nullptr;
    QVBoxLayout* favoritesGroupsLayout_ = nullptr;
    std::vector<CategoryWidget*> categories_;
    std::vector<FavoriteGroupView> favoriteGroups_;
    std::vector<EffectItemViews> effectItems_;
    GlobalModel *app_ = nullptr;
    int favoriteCount_ = 0;
    bool loadingConfig_ = false;
};
