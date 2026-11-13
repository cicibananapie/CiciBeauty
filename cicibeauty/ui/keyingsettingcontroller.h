// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <vector>
#include <string>
#include <QString>
#include <QWidget>

#include "ciciwidget.h"
#include "dynamicparamui.h"
#include "../modelapp.h"

class QPixmap;
class QCheckBox;
class QLabel;
class QSlider;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QStackedWidget;
class QTabWidget;
class ImageTextWidget;
class NokeyingZoneWidget;
class GlobalModel;

struct EffectDetailInfo2 : public EffectDetailInfo {
    const char* jsonKey_ = nullptr;

    void bind(QSlider* slider, QLineEdit* editor);
    void setJsonKey(const char* key);
    void setValue(uint32_t value);
    void syncValueToUI();
    void json2Me(const nlohmann::json& j, uint32_t defaultValue);
    void me2Json(nlohmann::json& j) const;
};

class KeyingTemplateController: public CiciWidget {
    Q_OBJECT

public:
    KeyingTemplateController(GlobalModel *app, const char* templateName, QWidget *parent = nullptr);
    ~KeyingTemplateController();
    
public:
    void active();
    void deactive();
    
public:
    void reset() override;
    void json2Me(const nlohmann::json& j) override;
    nlohmann::json me2Json() const;

signals:
    void valueFieldChanged(const QString& key, const nlohmann::json& value);
    void toggleFieldChanged(const QString& key, const nlohmann::json& value);
    
private:
    void bindSliderEditor(QSlider* slider, QLineEdit* editor, EffectDetailInfo2* pData);
    void initializeParams();
    std::u8string paramPath(const EffectDetailInfo2& param) const;
    void loadParams(bool defaultOnly, const nlohmann::json& config, std::function<void()> complete = {});
    void resetFeatureFlags();
    void applyFeatureFlags();
    void syncFeatureFlagUI();
    
private slots:
    void onResetParamBtnClicked();
    void onBlendSmoothStateChanged(int);
    void onLightShadowStateChanged(int);
    void onHairDetailStateChanged(int);
    void onColorBleedControlStateChanged(int);
    void onFaceProtectStateChanged(int);
    
private:
    EffectDetailInfo2 similarity_;
    EffectDetailInfo2 bkBlur_;
    EffectDetailInfo2 transparencyCorse_;
    EffectDetailInfo2 transparencyFine_;
    EffectDetailInfo2 edgeIndent_;
    EffectDetailInfo2 blendSmoothDegree_;
    EffectDetailInfo2 blendSmoothScope_;
    EffectDetailInfo2 lightShadowDegree_;
    EffectDetailInfo2 lightShadowScope_;
    EffectDetailInfo2 hairDetailScope_;
    EffectDetailInfo2 colorBleedRed_;
    EffectDetailInfo2 colorBleedGreen_;
    EffectDetailInfo2 colorBleedBlue_;
    EffectDetailInfo2 colorBleedMixCoefficient_;
    EffectDetailInfo2 colorBleedIntensity_;
    EffectDetailInfo2 colorBleedLuminance_;
    std::vector<EffectDetailInfo2*> params_;
    
    QList<QWidget*> blendSmoothUI_;
    QList<QWidget*> lightShadowUI_;
    QList<QWidget*> hairDetailUI_;
    QList<QWidget*> colorBleedUI_;
    QCheckBox* blendSmoothCheckBox_ = nullptr;
    QCheckBox* lightShadowCheckBox_ = nullptr;
    QCheckBox* hairDetailCheckBox_ = nullptr;
    QCheckBox* colorBleedCheckBox_ = nullptr;
    QCheckBox* faceProtectCheckBox_ = nullptr;
    bool blendSmoothEnabled_ = false;
    bool lightShadowEnabled_ = false;
    bool hairDetailEnabled_ = false;
    bool colorBleedEnabled_ = false;
    bool faceProtectEnabled_ = false;
    bool active_ = false;
    GlobalModel *app_ = nullptr;
    const char* templateName_ = nullptr;
    uint64_t paramQueryId_ = 0;
    bool loadingConfig_ = false;
};

class GreenScreenKeyingController: public CiciWidget {
    Q_OBJECT
    
    struct NoKeyingZone {
        QString tag;
        double x = 0;
        double y = 0;
        double width = 0;
        double height = 0;
        uint32_t type = 0;
        
        void json2Me(const nlohmann::json& j);
        nlohmann::json me2Json() const;
    };
    
public:
    GreenScreenKeyingController(GlobalModel *app, QWidget *parent = nullptr);
    ~GreenScreenKeyingController();
    
public:
    void createUI();
    void active();
    void deactive();
    
public:
    void reset() override;
    void json2Me(const nlohmann::json& j) override;
    nlohmann::json me2Json() const;

signals:
    void basicStateChanged(const QString& path, const nlohmann::json& value);
    void templateValueChanged(const QString& path, const nlohmann::json& value);
    void templateToggleChanged(const QString& path, const nlohmann::json& value);
    void nokeyingAssetChanged(const QString& path, const nlohmann::json& value);
    
private:
    void updateBackgroundAssetUI();
    QWidget* createColorPickWidget();
    QWidget* createNokeyingZoneWidget();
    void setColorLabel(uint32_t color);
    nlohmann::json maskShapeJson(const NoKeyingZone& zone) const;
    nlohmann::json zonesJson() const;
    void applyNokeyingZoneUiState();
    void syncNokeyingZoneWidgetFromState();
    void resetState(bool resetTemplates);
    void emitBasicStateFieldChanged(const QString& path, const nlohmann::json& value);
    void emitNokeyingAssetFieldChanged(const QString& path, const nlohmann::json& value);
    void emitSelectedColorChanged();
    void emitSelectedTemplateChanged();
    void emitBackgroundAssetChanged();
    void emitNokeyingZoneEnabledChanged();
    void emitZonesChanged();
    
private slots:
    void onCurrentChanged(int index);
    void onManualColorPickingBtnClicked();
    void onAIColorPickingBtnClicked();
    void onSetNoKeyingZoneBtnClicked();
    void onLoadLocalAssetBtnClicked();
    void onClearBackgroundAssetBtnClicked();
    void onColorPicked(unsigned int v);
    void onEnableNokeyingStateChanged(int);
    void onResetBtnClicked();
    void onRectangleBtnClicked();
    void onCircleBtnClicked();
    void onZoneCreated(const QString&, int, double, double, double, double);
    void onZoneDeleted(const QString&);
    
private:
    uint32_t colorPicked_ = (0 << 24) | (255 << 16) | (0 << 8) | 255;  //rgba;
    QString bkFile_;
    QLabel* colorPickLabel_ = nullptr;
    ImageTextWidget* bkAsset_ = nullptr;
    QLabel* assetNameLabel_ = nullptr;
    QPushButton* clearBkAssetBtn_ = nullptr;
    QWidget* keyingColorWindow_ = nullptr;
    QWidget* nokeyingZoneWindow_ = nullptr;
    
    QPushButton* resetBtn_ = nullptr;
    QPushButton* rectangleBtn_ = nullptr;
    QPushButton* circleBtn_ = nullptr;
    QCheckBox* nokeyingZoneCheckBox_ = nullptr;
    NokeyingZoneWidget* nokeyingZoneWidget_ = nullptr;
    KeyingTemplateController* template1_ = nullptr;
    KeyingTemplateController* template2_ = nullptr;
    QTabWidget* templateTabWidget_ = nullptr;
    std::vector<NoKeyingZone> zones_;
    bool nokeyingZoneEnabled_ = false;
    bool effectActive_ = false;
    GlobalModel *app_ = nullptr;
    bool loadingConfig_ = false;
};

class KeyingSettingController: public CiciWidget {
    Q_OBJECT
    
public:
    KeyingSettingController(GlobalModel *app, QWidget *parent = nullptr);
    ~KeyingSettingController();
    
public:
    void createUI();
    void switchLanguage();
    
public:
    void reset() override;
    void json2Me(const nlohmann::json& j) override;

signals:
    void settingPatchRequested(const nlohmann::json& patch);
    
private slots:
    void onAddLocalBtnClicked();
    
private:
    void resetState(bool resetGreenKeying);
    void emitKeyingModePatch(uint32_t mode);
    void emitGreenKeyingPatch(const QString& path, const nlohmann::json& value);
    void emitGreenKeyingBasicPatch(const QString& path, const nlohmann::json& value);
    void emitGreenKeyingTemplateValuePatch(const QString& path, const nlohmann::json& value);
    void emitGreenKeyingTemplateTogglePatch(const QString& path, const nlohmann::json& value);
    void emitGreenKeyingNokeyingAssetPatch(const QString& path, const nlohmann::json& value);
    
private:
    QRadioButton* noKeying_ = nullptr;
    QRadioButton* gsKeying_ = nullptr;
    QRadioButton* nsSegment_ = nullptr;
    QLabel* bkAssets_ = nullptr;
    QLabel* assetNameLabel_ = nullptr;
    GreenScreenKeyingController* gsKeyingTab_ = nullptr;
    QStackedWidget* stackedWidget_ = nullptr;
    GlobalModel *app_ = nullptr;
    bool loadingConfig_ = false;
};
