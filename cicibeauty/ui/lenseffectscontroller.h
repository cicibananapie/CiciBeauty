// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <QWidget>
#include <QList>
#include <QTabWidget>
#include <string>
#include <vector>

#include "ciciwidget.h"
#include "../obsinfo.h"
#include "../modelapp.h"
#include "../lumaclient.h"
#include "dynamicparamui.h"

class QLabel;
class QSlider;
class QLineEdit;
class CategorizedCardGrid;
class QTabWidget;
class ImageTextWidget;
class EffectItemWidget;
class GridMoreLayout;
class GlobalModel;

class BasicLensEffectsController: public CiciWidget, public IObsEventCallback, public IFilterModelChangeObserver {
    Q_OBJECT
    
public:
    BasicLensEffectsController(GlobalModel *app, QWidget *parent = nullptr);
    ~BasicLensEffectsController();
    
public:
    void reset() override;
    void json2Me(const nlohmann::json& j) override;

signals:
    void fieldChanged(const QString& key, const nlohmann::json& value);
    
private:
    void loadEffectConfig();
    
private:    //IObsEventCallback
    void onHVModeChanged(ObsInfo::HVMode mode) override;

private:    //IFilterModelChangeObserver
    void onFilterModelChanged(FilterModel* oldModel, FilterModel* newModel) override;
    
private slots:
    void onFaceTrackStateChanged(int);
    void onDualScreenClicked();
    void onTripleScreenClicked();
    void onbkBlurClicked();
    
private:
    ImageTextWidget* dualScreen_ = nullptr;
    ImageTextWidget* tripleScreen_ = nullptr;
    EffectItemWidget* bkBlur_ = nullptr;
    QCheckBox* faceTracking_ = nullptr;
    GlobalModel *app_ = nullptr;
};

class CameraMovementController: public CiciWidget, public IObsEventCallback, public IFilterModelChangeObserver {
    Q_OBJECT
    
    struct MovementData {
        uint32_t speed = 0;
        uint32_t amplitude = 0;
    };
public:
    CameraMovementController(GlobalModel *app, QWidget *parent = nullptr);
    ~CameraMovementController();
    
public:
    void reset() override;
    void json2Me(const nlohmann::json& j) override;

signals:
    void selectedChanged(uint32_t value);
    void movementFieldChanged(uint32_t effectId, const QString& key, const nlohmann::json& value);
    
private:    //IObsEventCallback
    void onHVModeChanged(ObsInfo::HVMode mode) override;

private:    //IFilterModelChangeObserver
    void onFilterModelChanged(FilterModel* oldModel, FilterModel* newModel) override;
    
private:
    void onItemClicked(EffectItemWidget* w);
    void setEffectParam(uint32_t which, int value);
    void bindSliderEditor(QSlider *slider, QLineEdit *editor, uint32_t cookie);
    void loadEffectList();
    void hideSlider();
    void showSlider();
    void refreshParamEnabledState();
    void selectWidget(EffectItemWidget* w);
    void restoreFromConfig(EffectItemWidget* w);
    
private:
    EffectItemWidget* selectedWidget_ = nullptr;
	DynamicParamUI dynamicUI_;
    GridMoreLayout* gridMoreLayout_ = nullptr;
    std::vector<EffectItemWidget*> widgets_;
    std::map<uint32_t, MovementData> movements_;
    GlobalModel *app_ = nullptr;
    LumaRequestScope paramRequests_;
    bool loadingConfig_ = false;
};

class ShotAtmosphereController: public QWidget, public IObsEventCallback, public IFilterModelChangeObserver {
    Q_OBJECT

public:
    ShotAtmosphereController(GlobalModel* app, QWidget* parent = nullptr);
    ~ShotAtmosphereController();

    void reset();
    void json2Me(const nlohmann::json& j);
    void reloadFavorites();

signals:
    void fieldChanged(const QString& key, const nlohmann::json& value);

private:
    void onHVModeChanged(ObsInfo::HVMode mode) override;
    void onFilterModelChanged(FilterModel* oldModel, FilterModel* newModel) override;

    struct Category {
        uint32_t id_;
        QString title_;
        QList<EffectItemWidget*> cards_;
    };

    void onCategoryActivated(const QString& view);
    void onItemClicked(EffectItemWidget* card);
    void loadEffectList();
    void refreshCategories();
    QList<QWidget*> favoriteCards() const;
    bool isEffectCompatible(uint32_t effectId) const;
    void setSelectedWidget(EffectItemWidget* card);
    void setEffectFavorite(EffectItemWidget* card, bool favorite);

    CategorizedCardGrid* cardGrid_ = nullptr;
    EffectItemWidget* selectedWidget_ = nullptr;
    std::vector<Category> categories_;
    GlobalModel* app_ = nullptr;
};

class LensEffectsController: public CiciWidget {
    Q_OBJECT
    
public:
    LensEffectsController(GlobalModel *app, QWidget *parent = nullptr);
    ~LensEffectsController();
    
public:
    void switchLanguage();
    void createUI();
    void reloadFavorites();
    
public:
    void reset() override;
    void json2Me(const nlohmann::json& j) override;

signals:
    void settingPatchRequested(const nlohmann::json& patch);
    
private slots:
    void onCurrentChanged(int);
    void onBasicLensFieldChanged(const QString& key, const nlohmann::json& value);
    void onCameraMovementSelectedChanged(uint32_t value);
    void onCameraMovementFieldChanged(uint32_t effectId, const QString& key, const nlohmann::json& value);
    void onShotAtmosphereFieldChanged(const QString& key, const nlohmann::json& value);
    
private:
    void resetState(bool resetChildren);

private:
    QTabWidget* tabWidget_ = nullptr;
    BasicLensEffectsController* basicLens_ = nullptr;
    CameraMovementController* cameraMovement_ = nullptr;
    ShotAtmosphereController* shotAtmosphere_ = nullptr;
    GlobalModel *app_ = nullptr;
    bool loadingConfig_ = false;
};
