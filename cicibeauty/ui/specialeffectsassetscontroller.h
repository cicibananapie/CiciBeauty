// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <stdint.h>
#include <vector>
#include <string>
#include <QString>
#include <QWidget>

#include "ciciwidget.h"
#include "../obsinfo.h"
#include "../modelapp.h"

class QLabel;
class QCheckBox;
class QStackedLayout;
class QTabWidget;
class EffectItemWidget;
class GridMoreLayout;
class ImageTextWidget;
class GlobalModel;

class SpCategoryWidget: public CiciWidget, public IObsEventCallback, public IFilterModelChangeObserver {
    Q_OBJECT
    
public:
    SpCategoryWidget(GlobalModel *app, QWidget *parent = nullptr);
    ~SpCategoryWidget();
    
public:
    void addEffects(const nlohmann::json& j);
    void selectEffect(uint32_t effectId);
    void setEffectFavorite(uint32_t effectId, bool favorite);
    uint32_t selectedEffectId() const;
    
public:
    void json2Me(const nlohmann::json& j) override;
    void reset() override;
    nlohmann::json me2Json() const;

signals:
    void selectionChanged(uint32_t clickedEffectId, uint32_t selectedEffectId);
    void favoriteChanged(uint32_t effectId, bool favorite);
    
private:    //IObsEventCallback
    void onHVModeChanged(ObsInfo::HVMode mode) override;

private:    //IFilterModelChangeObserver
    void onFilterModelChanged(FilterModel* oldModel, FilterModel* newModel) override;
    
private:
    void selectWidget(ImageTextWidget* w);
    
private:
    ImageTextWidget* selectedWidget_ = nullptr;
    std::vector<ImageTextWidget*> widgets_;
    GlobalModel *app_ = nullptr;
};

class SpecialEffectsAssetsController: public CiciWidget, public IObsEventCallback, public IFilterModelChangeObserver {
    Q_OBJECT
    
public:
    SpecialEffectsAssetsController(GlobalModel *app, QWidget *parent = nullptr);
    ~SpecialEffectsAssetsController();
    
public:
    void createUI();
    void switchLanguage();
    void reloadFavorites();
    
public:
    void reset() override;
    void json2Me(const nlohmann::json& j) override;
    nlohmann::json me2Json() const;

signals:
    void settingPatchRequested(const nlohmann::json& patch);

private:    //IObsEventCallback
    void onHVModeChanged(ObsInfo::HVMode mode) override;

private:    //IFilterModelChangeObserver
    void onFilterModelChanged(FilterModel* oldModel, FilterModel* newModel) override;
    
private:
    struct EffectItemViews {
        uint32_t effectId_ = 0;
        QString text_;
        QString iconPath_;
        SpCategoryWidget* category_ = nullptr;
        EffectItemWidget* favoriteWidget_ = nullptr;
    };

    void loadEffectList();
    void resetState(bool resetCategories);
    void setEffectFavorite(uint32_t effectId, bool favorite);
    void applyEffectFavorite(EffectItemViews& item, bool favorite);
    void syncFavoriteSelection(SpCategoryWidget* category, uint32_t selectedEffectId);
    EffectItemViews* effectItem(uint32_t effectId);
    bool isEffectCompatible(uint32_t effectId) const;
    
private slots:
    void onSoundStateChanged(int);
    void onCurrentChanged(int);
    
private:
    QCheckBox* enableAudio_ = nullptr;
    QLabel* cameraName_ = nullptr;
    QTabWidget* tabWidget_ = nullptr;
    QWidget* favoritesPage_ = nullptr;
    QStackedLayout* favoritesStack_ = nullptr;
    GridMoreLayout* favoritesGridLayout_ = nullptr;
    std::vector<SpCategoryWidget*> categories_;
    std::vector<EffectItemViews> effectItems_;
    GlobalModel *app_ = nullptr;
    int favoriteCount_ = 0;
    bool loadingConfig_ = false;
};
