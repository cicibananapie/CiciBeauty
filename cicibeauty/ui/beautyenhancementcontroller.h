// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <QWidget>

#include "ciciwidget.h"
#include "dynamicparamui.h"
#include "../modelapp.h"

class QLabel;
class QSlider;
class QLineEdit;
class QCheckBox;
class QRadioButton;
class QTabWidget;
class GlobalModel;

class SolutionController: public CiciWidget {
    Q_OBJECT
    
public:
    SolutionController(GlobalModel *app, QWidget *parent = nullptr);
    ~SolutionController();
    QString customName() const;
    QString displayName() const;
    void setCustomName(const QString& customName);
    
public:
    void json2Me(const nlohmann::json& j) override;
    void json2Me(const nlohmann::json& j, uint32_t solutionIndex);

signals:
    void fieldChanged(const QString& key, const nlohmann::json& value);
    
private:
    void createUI();
    void refreshEnableSkinState();
    void refreshEnableFaceShapingState();
    void initSmoothnessValues();
    void bindSliderEditor(QSlider* slider, QLineEdit* editor, EffectDetailInfo* pData);
    QWidget* createFaceTab();
    QWidget* createEyeTab();
    QWidget* createNoseTab();
    QWidget* createMouthTab();
    QWidget* createEyebrowTab();
    QWidget* createOtherTab();
    
private:
    QLabel* whitenLabel_ = nullptr;
    QSlider* whitenSlider_ = nullptr;
    QLineEdit* whitenEdit_ = nullptr;
    QLabel* clarityLabel_ = nullptr;
    QSlider* claritySlider_ = nullptr;
    QLineEdit* clarityEdit_ = nullptr;
    QLabel* smoothingLabel_ = nullptr;
    QSlider* smoothingSlider_ = nullptr;
    QLineEdit* smoothingEdit_ = nullptr;
    QCheckBox* skinBeauty_ = nullptr;
    QCheckBox* faceShaping_ = nullptr;
    QTabWidget* tabBodyShaping_ = nullptr;
    
    std::u8string name_;
    bool enableSkin_ = false;
    EffectDetailInfo whiteness_;
    EffectDetailInfo clarity_;
    EffectDetailInfo fullbodySmoothness_;
    bool enableFaceShaping_ = false;
    
    EffectDetailInfo slimFace_;
    EffectDetailInfo slimFaceLeft_;
    EffectDetailInfo slimFaceRight_;
    EffectDetailInfo smallFace_;
    EffectDetailInfo smallFaceLeft_;
    EffectDetailInfo smallFaceRight_;
    EffectDetailInfo faceSlimming_;
    EffectDetailInfo vFace_;
    EffectDetailInfo vFaceLeft_;
    EffectDetailInfo vFaceRight_;
    EffectDetailInfo hairLine_;
    EffectDetailInfo foreheadWidth_;
    EffectDetailInfo foreheadHeight_;
    EffectDetailInfo temple_;
    EffectDetailInfo templeLeft_;
    EffectDetailInfo templeRight_;
    EffectDetailInfo zygomaticBone_;
    EffectDetailInfo zygomaticBoneLeft_;
    EffectDetailInfo zygomaticBoneRight_;
    EffectDetailInfo mandible_;
    EffectDetailInfo mandibleLeft_;
    EffectDetailInfo mandibleRight_;
    EffectDetailInfo chin_;
    EffectDetailInfo pointedChin_;
    EffectDetailInfo pointedChinLeft_;
    EffectDetailInfo pointedChinRight_;
    
    EffectDetailInfo bigEye_;
    EffectDetailInfo bigEyeLeft_;
    EffectDetailInfo bigEyeRight_;
    EffectDetailInfo eyeHeight_;
    EffectDetailInfo eyeHeightLeft_;
    EffectDetailInfo eyeHeightRight_;
    EffectDetailInfo eyeWidth_;
    EffectDetailInfo eyeWidthLeft_;
    EffectDetailInfo eyeWidthRight_;
    EffectDetailInfo eyeDistance_;
    EffectDetailInfo eyeDistanceLeft_;
    EffectDetailInfo eyeDistanceRight_;
    EffectDetailInfo eyeCorner_;
    EffectDetailInfo eyeCornerLeft_;
    EffectDetailInfo eyeCornerRight_;
    EffectDetailInfo eyePos_;
    EffectDetailInfo eyePosLeft_;
    EffectDetailInfo eyePosRight_;
    
    EffectDetailInfo slimNose_;
    EffectDetailInfo slimNostril_;
    EffectDetailInfo slimNostrilLeft_;
    EffectDetailInfo slimNostrilRight_;
    EffectDetailInfo slimNoseBridge_;
    EffectDetailInfo straightenNose_;
    EffectDetailInfo nosePos_;
    
    EffectDetailInfo mouth_;
    EffectDetailInfo mouthTilt_;
    EffectDetailInfo philtrum_;
    EffectDetailInfo mouthPos_;
    
    EffectDetailInfo eyebrowThickness_;
    EffectDetailInfo eyebrowThicknessLeft_;
    EffectDetailInfo eyebrowThicknessRight_;
    EffectDetailInfo eyebrowDistance_;
    EffectDetailInfo eyebrowDistanceLeft_;
    EffectDetailInfo eyebrowDistanceRight_;
    EffectDetailInfo eyebrowTilt_;
    EffectDetailInfo eyebrowTiltLeft_;
    EffectDetailInfo eyebrowTiltRight_;
    EffectDetailInfo eyebrowPos_;
    EffectDetailInfo eyebrowPosLeft_;
    EffectDetailInfo eyebrowPosRight_;
    
    EffectDetailInfo foreheadLine_;
    EffectDetailInfo smileLine_;
    EffectDetailInfo whitenTeeth_;
    EffectDetailInfo darkCircle_;
    EffectDetailInfo brightenEye_;
    EffectDetailInfo midFace_;
    EffectDetailInfo lowerFace_;
    GlobalModel *app_ = nullptr;
    bool loadingConfig_ = false;
};

class QComboBox;
class BeautyEnhancementController: public CiciWidget {
    Q_OBJECT
    
public:
    BeautyEnhancementController(GlobalModel *app, QWidget *parent = nullptr);
    ~BeautyEnhancementController();
    
public:
    void switchLanguage();
    void createUI();
    
public:
    void reset() override;
    void json2Me(const nlohmann::json& j) override;

signals:
    void settingPatchRequested(const nlohmann::json& patch);
    
private:
    void refreshEnableState(bool b);
    void resetState(bool resetSolutions);
    bool isSolutionTab(int index) const;
    int solutionTabCount() const;
    int selectedSolutionIndexAfterRemove(int removedIndex) const;
    void refreshNewSolutionTabCloseButton();
    void emitSolutionFieldPatch(SolutionController* solution,
                                const QString& key,
                                const nlohmann::json& value);
    
private slots:
    void onTabCloseRequested(int index);
    void onTabBarClicked(int index);
    void onTabBarDoubleClicked(int index);
    void onCurrentChanged(int index);
    
private:
    QTabWidget* tabWidget_ = nullptr;
    QCheckBox* enableBeautyCheckBox_ = nullptr;
    QWidget* newSlnWidget_ = nullptr;
    GlobalModel *app_ = nullptr;
    bool loadingConfig_ = false;
};
