// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <stdint.h>

#include <map>
#include <QList>
#include <QString>
#include <QWidget>
#include <string>
#include <vector>

#include "ciciwidget.h"
#include "../obsinfo.h"
#include "../modelapp.h"
#include "dynamicparamui.h"

class QPixmap;
class QComboBox;
class QLabel;
class QSlider;
class QLineEdit;
class GridMoreLayout;
class QVBoxLayout;
class QHBoxLayout;
class ImageTextWidget;
class EffectItemWidget;
class GlobalModel;
class IEffectConflictResolver;

class StylizedMakeupController: public CiciWidget, public IObsEventCallback, public IFilterModelChangeObserver {
    Q_OBJECT
    
public:
    StylizedMakeupController(GlobalModel *app,
                             IEffectConflictResolver *conflictResolver,
                             QWidget *parent = nullptr);
    ~StylizedMakeupController();
    
public:
    void reset() override;
    void json2Me(const nlohmann::json& j) override;

signals:
    void fieldChanged(const QString& key, const nlohmann::json& value);
    
private:    //IObsEventCallback
    void onHVModeChanged(ObsInfo::HVMode mode) override;

private:    //IFilterModelChangeObserver
    void onFilterModelChanged(FilterModel* oldModel, FilterModel* newModel) override;
    
private:
    void onItemClicked(EffectItemWidget* w);
    void loadEffectList();
    void bindSliderEditor(QSlider* slider, QLineEdit* editor, uint32_t cookie);
    void selectWidget(EffectItemWidget* w);
    void applySelection(EffectItemWidget* w);
    void restoreFromConfig(EffectItemWidget* w);
    void hideParamUI();
    void showParamUI(uint32_t effectId);
    void refreshSelectedParamUi();
    void refreshParamEnabledState();
    QString makeupType2String(int type) const;
    void setEffectParam(uint32_t which, int value);
    
private:
    DynamicParamUI paramUI_;
    GridMoreLayout* gridMoreLayout_ = nullptr;
    EffectItemWidget* selectedWidget_ = nullptr;
    QList<EffectItemWidget*> widgets_;
    GlobalModel *app_ = nullptr;
    IEffectConflictResolver *conflictResolver_ = nullptr;
    bool loadingConfig_ = false;
};

class MakeupCategoryWidget: public CiciWidget, public IObsEventCallback, public IFilterModelChangeObserver {
    Q_OBJECT
    
public:
    MakeupCategoryWidget(GlobalModel *app,
                         IEffectConflictResolver *conflictResolver,
                         const nlohmann::json &j);
    ~MakeupCategoryWidget();
    
public:
    void setName(const QString& name);
    void addWidgets(const nlohmann::json& j);
    bool hasSelection() const;
    uint32_t selectedEffectId() const;
    void reset() override;
    void json2Me(const nlohmann::json& j) override;

signals:
    void selectedChanged();
    void strengthChanged(uint32_t value);
    
private slots:
    void onSliderValueChanged(int value);
    void onEditingFinished();
    
private:    //IObsEventCallback
    void onHVModeChanged(ObsInfo::HVMode mode) override;

private:    //IFilterModelChangeObserver
    void onFilterModelChanged(FilterModel* oldModel, FilterModel* newModel) override;
    
private:
    void onItemClicked(EffectItemWidget* w);
    void selectWidget(EffectItemWidget* w);
    void applySelection(EffectItemWidget* w);
    void restoreFromConfig(EffectItemWidget* w);
    void hideSlider();
    void showSlider();
    void refreshSelectedParamUi();
    void refreshParamEnabledState();
    
private:
    QLabel* categoryNameLabel_ = nullptr;
    DynamicParamUIItem dynamicParamUI_;
    GridMoreLayout* gridMoreLayout_ = nullptr;
    EffectItemWidget* selectedWidget_ = nullptr;
    QList<EffectItemWidget*> widgets_;
    GlobalModel *app_ = nullptr;
    IEffectConflictResolver *conflictResolver_ = nullptr;
    bool loadingConfig_ = false;
};

class CustomizedMakeupController: public CiciWidget {
    Q_OBJECT
public:
	CustomizedMakeupController(GlobalModel *app,
                             IEffectConflictResolver *conflictResolver,
                             QWidget *parent = nullptr);
    ~CustomizedMakeupController();
    
public:
    void reset() override;
    void json2Me(const nlohmann::json& j) override;

signals:
    void itemFieldChanged(uint32_t categoryId, const QString& key, const nlohmann::json& value);
    
private:
    void loadEffectList(QLayout* vbox);
    void resetState(bool resetCategories);
    void onCategorySelectedChanged(MakeupCategoryWidget* category);
    void onCategoryStrengthChanged(MakeupCategoryWidget* category, uint32_t value);
    
private:
    std::vector<MakeupCategoryWidget*> widgets_;
	GlobalModel *app_ = nullptr;
    IEffectConflictResolver *conflictResolver_ = nullptr;
};

class QTabWidget;
class MakeupSettingController: public CiciWidget {
    Q_OBJECT
    
public:
    MakeupSettingController(GlobalModel *app,
                            IEffectConflictResolver *conflictResolver,
                            QWidget *parent = nullptr);
    ~MakeupSettingController();
    
public:
    void createUI();
    
public:
    void reset() override;
    void json2Me(const nlohmann::json& j) override;

signals:
    void settingPatchRequested(const nlohmann::json& patch);
    
private slots:
    void onEnableMakeupStateChanged(int);
    void onCurrentChanged(int);
    void onStylizedFieldChanged(const QString& key, const nlohmann::json& value);
    void onCustomizedItemFieldChanged(uint32_t categoryId, const QString& key, const nlohmann::json& value);
    
private:
    void refreshEnableState();
    void resetState(bool resetChildren);
    
public:
    void switchLanguage();
    
private:
    QCheckBox* enableMakeup_ = nullptr;
    CustomizedMakeupController* customizedMakeup_ = nullptr;
    StylizedMakeupController* stylizedMakeup_ = nullptr;
    QTabWidget* tabWidget_ = nullptr;
    GlobalModel *app_ = nullptr;
    IEffectConflictResolver *conflictResolver_ = nullptr;
    bool loadingConfig_ = false;
};
