// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <stdint.h>

#include <QList>
#include <QString>
#include <QWidget>
#include <string>
#include <string_view>

#include "ciciwidget.h"
#include "../obsinfo.h"
#include "../modelapp.h"
#include "dynamicparamui.h"

class QLabel;
class QSlider;
class QLineEdit;
class EffectItemWidget;
class QHBoxLayout;
class QStackedLayout;
class QTabWidget;
class GridMoreLayout;
class GlobalModel;

class FilterSettingController: public CiciWidget, public IObsEventCallback, public IFilterModelChangeObserver {
    Q_OBJECT
    
public:
    FilterSettingController(GlobalModel *app,
                            IEffectConflictResolver *conflictResolver,
                            QWidget *parent = nullptr);
    ~FilterSettingController();
    
public:
    void switchLanguage();
    void createUI();
    void reloadFavorites();
    
public:
    void reset() override;
    void json2Me(const nlohmann::json& j) override;

signals:
    void settingPatchRequested(const nlohmann::json& patch);
    
private:    //IObsEventCallback
    void onHVModeChanged(ObsInfo::HVMode mode) override;

private:    //IFilterModelChangeObserver
    void onFilterModelChanged(FilterModel* oldModel, FilterModel* newModel) override;
    
private slots:
    void onSliderValueChanged(int value);
    void onEditingFinished();
    void onEnableFilterStateChanged(int state);
    void onCurrentTabChanged(int index);
    
private:
    struct EffectItemViews {
        EffectItemWidget* allWidget_ = nullptr;
        EffectItemWidget* favoriteWidget_ = nullptr;
        QString iconPath_;
    };

    void loadEffectList();
    void refreshEnableState();
    void refreshParamEnabledState();
    void selectWidget(EffectItemWidget* w, std::u8string_view source = u8"all");
    void applySelection(EffectItemWidget* w);
    void setSelectedWidget(EffectItemWidget* w);
    void restoreFromConfig(EffectItemWidget* w);
    void refreshSelectedParamUi();
    void hideSlider();
    void showSlider();
    void setEffectFavorite(uint64_t effectId, bool favorite);
    void applyEffectFavorite(EffectItemViews& item, bool favorite);
    EffectItemViews* effectItem(uint64_t effectId);
    bool isEffectCompatible(uint64_t effectId) const;
    
private:
    QCheckBox* enableFilter_ = nullptr;
    QTabWidget* effectTabs_ = nullptr;
    QStackedLayout* favoritesStack_ = nullptr;
    DynamicParamUIItem dynamicParamUI_;
    DynamicParamUIItem favoriteDynamicParamUI_;
    GridMoreLayout* gridMoreLayout_ = nullptr;
    GridMoreLayout* favoritesGridLayout_ = nullptr;
    EffectItemWidget* selectedWidget_ = nullptr;
    QList<EffectItemViews> effectItems_;
    GlobalModel *app_ = nullptr;
    IEffectConflictResolver *conflictResolver_ = nullptr;
    int favoriteCount_ = 0;
    bool loadingConfig_ = false;
};
