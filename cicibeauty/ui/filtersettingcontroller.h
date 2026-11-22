// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <stdint.h>

#include <QList>
#include <QString>
#include <QWidget>

#include "ciciwidget.h"
#include "../obsinfo.h"
#include "../modelapp.h"
#include "../lumaclient.h"

class CategorizedCardGrid;
class EffectItemWidget;
class GlobalModel;
class QLabel;
class QLineEdit;
class QSlider;

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
    void onCategoryActivated(const QString& id);
    
private:
    struct EffectItem {
        EffectItemWidget* widget_;
        QString category_;
    };

    void loadEffectList();
    void refreshCategories();
    QList<QWidget*> favoriteCards() const;
    void refreshEnableState();
    void refreshParamEnabledState();
    void selectWidget(EffectItemWidget* w);
    void applySelection(EffectItemWidget* w);
    void setSelectedWidget(EffectItemWidget* w);
    void restoreFromConfig(EffectItemWidget* w);
    void refreshSelectedParamUi();
    void resetStrengthUi();
    void setEffectFavorite(EffectItemWidget* widget, bool favorite);
    bool isEffectCompatible(uint64_t effectId) const;
    
private:
    QCheckBox* enableFilter_ = nullptr;
    CategorizedCardGrid* effectGrid_ = nullptr;
    QLabel* currentFilter_ = nullptr;
    QWidget* strengthRow_ = nullptr;
    QSlider* strengthSlider_ = nullptr;
    QLineEdit* strengthEditor_ = nullptr;
    EffectItemWidget* selectedWidget_ = nullptr;
    QList<EffectItem> effectItems_;
    GlobalModel *app_ = nullptr;
    IEffectConflictResolver *conflictResolver_ = nullptr;
    QString defaultCategory_;
    LumaRequestScope paramRequests_;
    bool loadingConfig_ = false;
};
