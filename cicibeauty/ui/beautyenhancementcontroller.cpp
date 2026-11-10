// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include <assert.h>
#include <iterator>
#include <vector>
#include <QString>
#include <QLabel>
#include <QSlider>
#include <QLineEdit>
#include <QIntValidator>
#include <QRadioButton>
#include <QComboBox>
#include <QCheckBox>
#include <QTabWidget>
#include <QInputDialog>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include "log.h"
#include "misc.h"
#include "nlohmann/json.hpp"
#include "commonui.h"
#include "filtermodelhelpers.h"
#include "beautyenhancementcontroller.h"
#include "solutiontabhelper.h"
#include "../modelapp.h"
#include "../qstringconv.h"
#include "../settingconfig.h"
#include "jsonutility.h"
#include "../statistics/settingspageanalytics.h"

namespace {
void syncEffectDetailUi(const EffectDetailInfo& info) {
    QSignalBlocker sliderBlocker(info.slider_);
    info.slider_->setValue(static_cast<int>(info.value_));
    QSignalBlocker editorBlocker(info.editor_);
    info.editor_->setText(QString::number(info.value_));
}

std::u8string beautyParamPath(uint32_t solutionIndex, const char* key) {
    return std::u8string(u8"/beauty/solutions/") + to_u8(solutionIndex) + u8"/" + to_u8(key);
}

uint32_t resolvedEffectParamValue(GlobalModel* app, const std::u8string& path) {
    int value = 0;
	auto *model = app->filterModel();
	if (model->effectParamValue(path.c_str(), value) && value >= 0) {
		return value;
	}

    assert(false);
	return value;
}

nlohmann::json defaultBeautySolution(GlobalModel* app) {
    const nlohmann::json config = app->settingConfig()->defaultConfig();
    const auto beautyIt = config.find("beauty");
    if (beautyIt != config.end() && beautyIt->is_object()) {
        const auto solutionsIt = beautyIt->find("solutions");
        if (solutionsIt != beautyIt->end() &&
            solutionsIt->is_array() &&
            !solutionsIt->empty() &&
            (*solutionsIt)[0].is_object()) {
            return (*solutionsIt)[0];
        }
    }

    assert(false);
    LogError << "BeautyEnhancementController::defaultBeautySolution missing default solution"
             << logEnd;
    return nlohmann::json::object();
}

nlohmann::json beautySolutionSkeleton(GlobalModel* app, uint32_t id, const std::u8string& name = {}) {
    auto j = defaultBeautySolution(app);
    j["id"] = id;
    j["customName"] = to_json(name);
    return j;
}

}

SolutionController::SolutionController(GlobalModel* app, QWidget* parent/* = nullptr*/)
: CiciWidget(parent), app_(app) {
    createUI();
}

QString SolutionController::customName() const {
    return from_u8(name_);
}

QString SolutionController::displayName() const {
    const QString name = customName();
    return name.isEmpty() ? QString("%1 %2").arg(tr("Sln")).arg(id()) : name;
}

void SolutionController::setCustomName(const QString& customName) {
    name_ = to_u8(customName.trimmed());
}

void SolutionController::createUI() {
    skinBeauty_ = createCheckBox(tr("Skin Beauty"));
    
    whitenLabel_ = createFixWidthMarqueeLabel(tr("Whitening"));
    whitenSlider_ = createSlider();
    whitenEdit_ = createEdit();
    bindSliderEditor(whitenSlider_, whitenEdit_, &whiteness_);
    QHBoxLayout*hboxWhiten = new QHBoxLayout;
    hboxWhiten->setSpacing(KSpacing);
    hboxWhiten->addItem(new QSpacerItem(KHeadSpading, 20, QSizePolicy::Fixed, QSizePolicy::Fixed));
    hboxWhiten->addWidget(whitenLabel_);
    hboxWhiten->addWidget(whitenSlider_);
    hboxWhiten->addWidget(whitenEdit_);
    hboxWhiten->addSpacing(KTailSpacing);
    
    clarityLabel_ = createFixWidthMarqueeLabel(tr("Clarity"));
    claritySlider_ = createSlider();
    clarityEdit_ = createEdit();
    bindSliderEditor(claritySlider_, clarityEdit_, &clarity_);
    QHBoxLayout*hboxClarity = new QHBoxLayout;
    hboxClarity->setSpacing(KSpacing);
    hboxClarity->addItem(new QSpacerItem(KHeadSpading, 20, QSizePolicy::Fixed, QSizePolicy::Fixed));
    hboxClarity->addWidget(clarityLabel_);
    hboxClarity->addWidget(claritySlider_);
    hboxClarity->addWidget(clarityEdit_);
    hboxClarity->addSpacing(KTailSpacing);
    
    smoothingLabel_ = createFixWidthMarqueeLabel(tr("Smoothing"));
    smoothingSlider_ = createSlider();
    smoothingEdit_ = createEdit();
    bindSliderEditor(smoothingSlider_, smoothingEdit_, &fullbodySmoothness_);
    QHBoxLayout*hboxSmoothing = new QHBoxLayout;
    hboxSmoothing->setSpacing(KSpacing);
    hboxSmoothing->addItem(new QSpacerItem(KHeadSpading, 20, QSizePolicy::Fixed, QSizePolicy::Fixed));
    hboxSmoothing->addWidget(smoothingLabel_);
    hboxSmoothing->addWidget(smoothingSlider_);
    hboxSmoothing->addWidget(smoothingEdit_);
    hboxSmoothing->addSpacing(KTailSpacing);
    
    
    faceShaping_ = createCheckBox(tr("Body Shaping"));
    
    tabBodyShaping_ = new QTabWidget;
    applyDisabledTabStyle(tabBodyShaping_);
    tabBodyShaping_->setContentsMargins(0, 0, 0, 0);
    tabBodyShaping_->addTab(createFaceTab(), tr("Face"));
    tabBodyShaping_->addTab(createEyeTab(), tr("Eye"));
    tabBodyShaping_->addTab(createNoseTab(), tr("Nose"));
    tabBodyShaping_->addTab(createMouthTab(), tr("Mouth"));
    tabBodyShaping_->addTab(createEyebrowTab(), tr("Eyebrow"));
    tabBodyShaping_->addTab(createOtherTab(), tr("Other"));
    
    QVBoxLayout* vboxTop = new QVBoxLayout;
    vboxTop->setContentsMargins(0, 0, 0, 0);
    vboxTop->addWidget(skinBeauty_);
    vboxTop->addItem(hboxWhiten);
    vboxTop->addItem(hboxClarity);
    vboxTop->addItem(hboxSmoothing);
    vboxTop->addWidget(faceShaping_);
    vboxTop->addWidget(tabBodyShaping_);
    
    this->setLayout(vboxTop);
    this->setContentsMargins(0, 0, 0, 0);
    
    QObject::connect(skinBeauty_, &QCheckBox::stateChanged, this, [this](int state) {
        enableSkin_ = (state == Qt::Checked);
        refreshEnableSkinState();
        if (!loadingConfig_) {
            emit fieldChanged(QStringLiteral("enableSkin"), enableSkin_);
        }
    });
    QObject::connect(faceShaping_, &QCheckBox::stateChanged, this, [this](int state) {
        enableFaceShaping_ = (state == Qt::Checked);
        refreshEnableFaceShapingState();
        if (!loadingConfig_) {
            emit fieldChanged(QStringLiteral("enableFaceShaping"), enableFaceShaping_);
        }
    });
    QObject::connect(skinBeauty_, &QCheckBox::clicked, this, [this](bool checked) {
        reportBeautyEnhancementFeatureToggle(app_,
                                             id(),
                                             to_u8(displayName()),
                                             u8"skin_beauty",
                                             checked);
    });
    QObject::connect(faceShaping_, &QCheckBox::clicked, this, [this](bool checked) {
        reportBeautyEnhancementFeatureToggle(app_,
                                             id(),
                                             to_u8(displayName()),
                                             u8"face_shaping",
                                             checked);
    });
    QObject::connect(tabBodyShaping_, &QTabWidget::tabBarClicked, this, [this](int index) {
        constexpr std::u8string_view sections[] = {
            u8"face", u8"eye", u8"nose", u8"mouth", u8"eyebrow", u8"other",
        };
        if (index < 0 || index >= static_cast<int>(std::size(sections))) {
            return;
        }
        reportBeautyEnhancementSectionSelect(app_,
                                             id(),
                                             to_u8(displayName()),
                                             sections[index]);
    });
    
    refreshEnableSkinState();
    refreshEnableFaceShapingState();
    initSmoothnessValues();
}

SolutionController::~SolutionController() {
    
}

void SolutionController::json2Me(const nlohmann::json& j) {
    json2Me(j, 0);
}

void SolutionController::json2Me(const nlohmann::json& j, uint32_t solutionIndex) {
    loadingConfig_ = true;
    auto readValue = [this, solutionIndex](EffectDetailInfo& info, const char* key) {
        info.jsonKey_ = key;
        info.value_ = resolvedEffectParamValue(app_, beautyParamPath(solutionIndex, key));
        syncEffectDetailUi(info);
    };

    setId(j.value("id", 1));
    name_ = to_u8(from_json(j.value("customName", nlohmann::json())));
    enableSkin_ = j.value("enableSkin", false);
    readValue(whiteness_, "whiteness");
    readValue(clarity_, "clarity");
    readValue(fullbodySmoothness_, "fullbodySmoothness");
    enableFaceShaping_ = j.value("enableFaceShaping", false);
    
    readValue(slimFace_, "slimFace");
    readValue(slimFaceLeft_, "slimFaceLeft");
    readValue(slimFaceRight_, "slimFaceRight");
    readValue(smallFace_, "smallFace");
    readValue(smallFaceLeft_, "smallFaceLeft");
    readValue(smallFaceRight_, "smallFaceRight");
    readValue(faceSlimming_, "faceSlimming");
    readValue(vFace_, "VFace");
    readValue(vFaceLeft_, "VFaceLeft");
    readValue(vFaceRight_, "VFaceRight");
    readValue(hairLine_, "hairLine");
    readValue(foreheadWidth_, "foreheadWidth");
    readValue(foreheadHeight_, "foreheadHeight");
    readValue(temple_, "temple");
    readValue(templeLeft_, "leftTemple");
    readValue(templeRight_, "rightTemple");
    readValue(zygomaticBone_, "zygomaticBone");
    readValue(zygomaticBoneLeft_, "zygomaticBoneLeft");
    readValue(zygomaticBoneRight_, "zygomaticBoneRight");
    readValue(mandible_, "mandible");
    readValue(mandibleLeft_, "mandibleLeft");
    readValue(mandibleRight_, "mandibleRight");
    readValue(chin_, "chin");
    readValue(pointedChin_, "pointedChin");
    readValue(pointedChinLeft_, "pointedChinLeft");
    readValue(pointedChinRight_, "pointedChinRight");
    
    readValue(bigEye_, "bigEye");
    readValue(bigEyeLeft_, "bigEyeLeft");
    readValue(bigEyeRight_, "bigEyeRight");
    readValue(eyeHeight_, "eyeHeight");
    readValue(eyeHeightLeft_, "eyeHeightLeft");
    readValue(eyeHeightRight_, "eyeHeightRight");
    readValue(eyeWidth_, "eyeWidth");
    readValue(eyeWidthLeft_, "eyeWidthLeft");
    readValue(eyeWidthRight_, "eyeWidthRight");
    readValue(eyeDistance_, "eyeDistance");
    readValue(eyeDistanceLeft_, "eyeDistanceLeft");
    readValue(eyeDistanceRight_, "eyeDistanceRight");
    readValue(eyeCorner_, "eyeCorner");
    readValue(eyeCornerLeft_, "eyeCornerLeft");
    readValue(eyeCornerRight_, "eyeCornerRight");
    readValue(eyePos_, "eyePos");
    readValue(eyePosLeft_, "eyePosLeft");
    readValue(eyePosRight_, "eyePosRight");
    
    readValue(slimNose_, "slimNose");
    readValue(slimNostril_, "slimNostril");
    readValue(slimNostrilLeft_, "slimNostrilLeft");
    readValue(slimNostrilRight_, "slimNostrilRight");
    readValue(slimNoseBridge_, "slimNoseBridge");
    readValue(straightenNose_, "straightenNose");
    readValue(nosePos_, "nosePos");
    
    readValue(mouth_, "mouth");
    readValue(mouthTilt_, "mouthTilt");
    readValue(philtrum_, "philtrum");
    readValue(mouthPos_, "mouthPos");
    
    readValue(eyebrowThickness_, "eyebrowThickness");
    readValue(eyebrowThicknessLeft_, "eyebrowThicknessLeft");
    readValue(eyebrowThicknessRight_, "eyebrowThicknessRight");
    readValue(eyebrowDistance_, "eyebrowDistance");
    readValue(eyebrowDistanceLeft_, "eyebrowDistanceLeft");
    readValue(eyebrowDistanceRight_, "eyebrowDistanceRight");
    readValue(eyebrowTilt_, "eyebrowTilt");
    readValue(eyebrowTiltLeft_, "eyebrowTiltLeft");
    readValue(eyebrowTiltRight_, "eyebrowTiltRight");
    readValue(eyebrowPos_, "eyebrowPos");
    readValue(eyebrowPosLeft_, "eyebrowPosLeft");
    readValue(eyebrowPosRight_, "eyebrowPosRight");
    
    readValue(foreheadLine_, "foreheadLine");
    readValue(smileLine_, "smileLine");
    readValue(whitenTeeth_, "whitenTeeth");
    readValue(darkCircle_, "darkCircle");
    readValue(brightenEye_, "brightenEye");
    readValue(midFace_, "midFace");
    readValue(lowerFace_, "lowerFace");

    {
        QSignalBlocker blocker(skinBeauty_);
        skinBeauty_->setCheckState(enableSkin_ ? Qt::Checked : Qt::Unchecked);
    }
    {
        QSignalBlocker blocker(faceShaping_);
        faceShaping_->setCheckState(enableFaceShaping_ ? Qt::Checked : Qt::Unchecked);
    }
    refreshEnableSkinState();
    refreshEnableFaceShapingState();
    loadingConfig_ = false;
}

void SolutionController::bindSliderEditor(QSlider* slider, QLineEdit* editor, EffectDetailInfo* pData) {
    assert(pData != nullptr);
    pData->slider_ = slider;
    pData->editor_ = editor;
    slider->setValue(pData->value_);
    editor->setText(QString::number(pData->value_));
    
    QObject::connect(slider, &QSlider::valueChanged, this, [this, editor, pData](int value) {
        editor->setText(QString::number(value));
        pData->value_ = value;
        if (!loadingConfig_) {
            assert(pData->jsonKey_ != nullptr);
            emit fieldChanged(QString::fromUtf8(pData->jsonKey_), static_cast<uint32_t>(value));
        }
    });
    QObject::connect(editor, &QLineEdit::editingFinished, this, [this, slider, editor, pData]() {
        const bool userEdited = editor->isModified();
        editor->setModified(false);
        int value = editor->text().toUInt();
        if (slider->value() != value) {
            QSignalBlocker blocker(slider);
            slider->setValue(value);
            pData->value_ = value;
            if (!loadingConfig_) {
                assert(pData->jsonKey_ != nullptr);
                emit fieldChanged(QString::fromUtf8(pData->jsonKey_), static_cast<uint32_t>(value));
                if (userEdited) {
                    reportBeautyEnhancementParamInput(app_,
                                                      id(),
                                                      to_u8(displayName()),
                                                      to_u8(pData->jsonKey_),
                                                      value);
                }
            }
        }
    });
}

void SolutionController::initSmoothnessValues() {
    smoothingSlider_->setValue(fullbodySmoothness_.value_);
    smoothingEdit_->setText(QString::number(fullbodySmoothness_.value_));
}

QWidget* SolutionController::createFaceTab() {
    QVBoxLayout* vbox = new QVBoxLayout;
    QWidget *w = new QWidget;
    w->setContentsMargins(0, 0, 0, 0);
    
    QLabel* slimFaceLabel = createFixWidthMarqueeLabel(tr("Slim face"));
    QSlider* slimFaceSlider = createSlider();
    QLineEdit* slimFaceEdit = createEdit();
    bindSliderEditor(slimFaceSlider, slimFaceEdit, &slimFace_);
    QHBoxLayout* slimFaceHbox = new QHBoxLayout;
    slimFaceHbox->setSpacing(KSpacing);
    slimFaceHbox->addWidget(slimFaceLabel);
    slimFaceHbox->addWidget(slimFaceSlider);
    slimFaceHbox->addWidget(slimFaceEdit);
    vbox->addItem(slimFaceHbox);
    {
        auto slimFaceLeftLabel = createFixWidthMarqueeLabel(tr("Slim Face Left"));
        auto slimFaceLeftSlider = createSlider();
        auto slimFaceLeftEdit = createEdit();
        bindSliderEditor(slimFaceLeftSlider, slimFaceLeftEdit, &slimFaceLeft_);
        auto slimFaceLeftHBox = new QHBoxLayout;
        slimFaceLeftHBox->setSpacing(KSpacing);
        slimFaceLeftHBox->addItem(createFixedHorizontalSpacer(KHeadSpading));
        slimFaceLeftHBox->addWidget(slimFaceLeftLabel);
        slimFaceLeftHBox->addWidget(slimFaceLeftSlider);
        slimFaceLeftHBox->addWidget(slimFaceLeftEdit);
        vbox->addItem(slimFaceLeftHBox);
        
        auto slimFaceRightLabel = createFixWidthMarqueeLabel(tr("Slim Face Right"));
        auto slimFaceRightSlider = createSlider();
        auto slimFaceRightEdit = createEdit();
        bindSliderEditor(slimFaceRightSlider, slimFaceRightEdit, &slimFaceRight_);
        auto slimFaceRightHBox = new QHBoxLayout;
        slimFaceRightHBox->setSpacing(KSpacing);
        slimFaceRightHBox->addItem(createFixedHorizontalSpacer(KHeadSpading));
        slimFaceRightHBox->addWidget(slimFaceRightLabel);
        slimFaceRightHBox->addWidget(slimFaceRightSlider);
        slimFaceRightHBox->addWidget(slimFaceRightEdit);
        vbox->addItem(slimFaceRightHBox);
    }
    
    QLabel* smallFaceLabel = createFixWidthMarqueeLabel(tr("Small face"));
    QSlider* smallFaceSlider = createSlider();
    QLineEdit* smallFaceEdit = createEdit();
    bindSliderEditor(smallFaceSlider, smallFaceEdit, &smallFace_);
    QHBoxLayout* smallFaceHbox = new QHBoxLayout;
    smallFaceHbox->setSpacing(KSpacing);
    smallFaceHbox->addWidget(smallFaceLabel);
    smallFaceHbox->addWidget(smallFaceSlider);
    smallFaceHbox->addWidget(smallFaceEdit);
    vbox->addItem(smallFaceHbox);
    {
        auto smallFaceLeftLabel = createFixWidthMarqueeLabel(tr("Small Face Left"));
        auto smallFaceLeftSlider = createSlider();
        auto smallFaceLeftEdit = createEdit();
        bindSliderEditor(smallFaceLeftSlider, smallFaceLeftEdit, &smallFaceLeft_);
        auto smallFaceLeftHBox = new QHBoxLayout;
        smallFaceLeftHBox->setSpacing(KSpacing);
        smallFaceLeftHBox->addItem(createFixedHorizontalSpacer(KHeadSpading));
        smallFaceLeftHBox->addWidget(smallFaceLeftLabel);
        smallFaceLeftHBox->addWidget(smallFaceLeftSlider);
        smallFaceLeftHBox->addWidget(smallFaceLeftEdit);
        vbox->addItem(smallFaceLeftHBox);
        
        auto smallFaceRightLabel = createFixWidthMarqueeLabel(tr("Small Face Right"));
        auto smallFaceRightSlider = createSlider();
        auto smallFaceRightEdit = createEdit();
        bindSliderEditor(smallFaceRightSlider, smallFaceRightEdit, &smallFaceRight_);
        auto smallFaceRightHBox = new QHBoxLayout;
        smallFaceRightHBox->setSpacing(KSpacing);
        smallFaceRightHBox->addItem(createFixedHorizontalSpacer(KHeadSpading));
        smallFaceRightHBox->addWidget(smallFaceRightLabel);
        smallFaceRightHBox->addWidget(smallFaceRightSlider);
        smallFaceRightHBox->addWidget(smallFaceRightEdit);
        vbox->addItem(smallFaceRightHBox);
    }
    
    QLabel* faceSlimmingLabel = createFixWidthMarqueeLabel(tr("Face slimming"));
    QSlider* faceSlimmingSlider = createSlider();
    QLineEdit* faceSlimmingEdit = createEdit();
    bindSliderEditor(faceSlimmingSlider, faceSlimmingEdit, &faceSlimming_);
    QHBoxLayout* faceSlimmingHbox = new QHBoxLayout;
    faceSlimmingHbox->setSpacing(KSpacing);
    faceSlimmingHbox->addWidget(faceSlimmingLabel);
    faceSlimmingHbox->addWidget(faceSlimmingSlider);
    faceSlimmingHbox->addWidget(faceSlimmingEdit);
    vbox->addItem(faceSlimmingHbox);
    
    QLabel* vFaceLabel = createFixWidthMarqueeLabel(tr("V Face"));
    QSlider* vFaceSlider = createSlider();
    QLineEdit* vFaceEdit = createEdit();
    bindSliderEditor(vFaceSlider, vFaceEdit, &vFace_);
    QHBoxLayout* vFaceHbox = new QHBoxLayout;
    vFaceHbox->setSpacing(KSpacing);
    vFaceHbox->addWidget(vFaceLabel);
    vFaceHbox->addWidget(vFaceSlider);
    vFaceHbox->addWidget(vFaceEdit);
    vbox->addItem(vFaceHbox);
    
    {
        auto vFaceLeftLabel = createFixWidthMarqueeLabel(tr("V Face Left"));
        auto vFaceLeftSlider = createSlider();
        auto vFaceLeftEdit = createEdit();
        bindSliderEditor(vFaceLeftSlider, vFaceLeftEdit, &vFaceLeft_);
        auto vFaceLeftHBox = new QHBoxLayout;
        vFaceLeftHBox->setSpacing(KSpacing);
        vFaceLeftHBox->addItem(createFixedHorizontalSpacer(KHeadSpading));
        vFaceLeftHBox->addWidget(vFaceLeftLabel);
        vFaceLeftHBox->addWidget(vFaceLeftSlider);
        vFaceLeftHBox->addWidget(vFaceLeftEdit);
        vbox->addItem(vFaceLeftHBox);
        
        auto vFaceRightLabel = createFixWidthMarqueeLabel(tr("V Face Right"));
        auto vFaceRightSlider = createSlider();
        auto vFaceRightEdit = createEdit();
        bindSliderEditor(vFaceRightSlider, vFaceRightEdit, &vFaceRight_);
        auto vFaceRightHBox = new QHBoxLayout;
        vFaceRightHBox->setSpacing(KSpacing);
        vFaceRightHBox->addItem(createFixedHorizontalSpacer(KHeadSpading));
        vFaceRightHBox->addWidget(vFaceRightLabel);
        vFaceRightHBox->addWidget(vFaceRightSlider);
        vFaceRightHBox->addWidget(vFaceRightEdit);
        vbox->addItem(vFaceRightHBox);
    }
    
    QLabel* hairlineLabel = createFixWidthMarqueeLabel(tr("Hairline"));
    QSlider* hairlineSlider = createSlider();
    QLineEdit* hairlineEdit = createEdit();
    bindSliderEditor(hairlineSlider, hairlineEdit, &hairLine_);
    QHBoxLayout* hairlineHbox = new QHBoxLayout;
    hairlineHbox->setSpacing(KSpacing);
    hairlineHbox->addWidget(hairlineLabel);
    hairlineHbox->addWidget(hairlineSlider);
    hairlineHbox->addWidget(hairlineEdit);
    vbox->addItem(hairlineHbox);
    
    QLabel* foreheadWidthLabel = createFixWidthMarqueeLabel(tr("Forehead Width"));
    QSlider* foreheadWidthSlider = createSlider();
    QLineEdit* foreheadWidthEdit = createEdit();
    bindSliderEditor(foreheadWidthSlider, foreheadWidthEdit, &foreheadWidth_);
    QHBoxLayout* foreheadWidthHbox = new QHBoxLayout;
    foreheadWidthHbox->setSpacing(KSpacing);
    foreheadWidthHbox->addWidget(foreheadWidthLabel);
    foreheadWidthHbox->addWidget(foreheadWidthSlider);
    foreheadWidthHbox->addWidget(foreheadWidthEdit);
    vbox->addItem(foreheadWidthHbox);
    
    QLabel* foreheadHeightLabel = createFixWidthMarqueeLabel(tr("Forehead Height"));
    QSlider* foreheadHeightSlider = createSlider();
    QLineEdit* foreheadHeightEdit = createEdit();
    bindSliderEditor(foreheadHeightSlider, foreheadHeightEdit, &foreheadHeight_);
    QHBoxLayout* foreheadHeightHbox = new QHBoxLayout;
    foreheadHeightHbox->setSpacing(KSpacing);
    foreheadHeightHbox->addWidget(foreheadHeightLabel);
    foreheadHeightHbox->addWidget(foreheadHeightSlider);
    foreheadHeightHbox->addWidget(foreheadHeightEdit);
    vbox->addItem(foreheadHeightHbox);
    
    QLabel* templeLabel = createFixWidthMarqueeLabel(tr("Temple"));
    QSlider* templeSlider = createSlider();
    QLineEdit* templeEdit = createEdit();
    bindSliderEditor(templeSlider, templeEdit, &temple_);
    QHBoxLayout* templeHbox = new QHBoxLayout;
    templeHbox->setSpacing(KSpacing);
    templeHbox->addWidget(templeLabel);
    templeHbox->addWidget(templeSlider);
    templeHbox->addWidget(templeEdit);
    vbox->addItem(templeHbox);
    {
        QLabel* leftTempleLabel = createFixWidthMarqueeLabel(tr("Left Temple"));
        QSlider* leftTempleSlider = createSlider();
        QLineEdit* leftTempleEdit = createEdit();
        bindSliderEditor(leftTempleSlider, leftTempleEdit, &templeLeft_);
        QHBoxLayout* leftTempleHbox = new QHBoxLayout;
        leftTempleHbox->setSpacing(KSpacing);
        leftTempleHbox->addWidget(leftTempleLabel);
        leftTempleHbox->addWidget(leftTempleSlider);
        leftTempleHbox->addWidget(leftTempleEdit);
        vbox->addItem(leftTempleHbox);
        
        QLabel* rightTempleLabel = createFixWidthMarqueeLabel(tr("Right Temple"));
        QSlider* rightTempleSlider = createSlider();
        QLineEdit* rightTempleEdit = createEdit();
        bindSliderEditor(rightTempleSlider, rightTempleEdit, &templeRight_);
        QHBoxLayout* rightTempleHbox = new QHBoxLayout;
        rightTempleHbox->setSpacing(KSpacing);
        rightTempleHbox->addWidget(rightTempleLabel);
        rightTempleHbox->addWidget(rightTempleSlider);
        rightTempleHbox->addWidget(rightTempleEdit);
        vbox->addItem(rightTempleHbox);
    }
    
    QLabel* zygomaticBoneLabel = createFixWidthMarqueeLabel(tr("Zygomatic Bone"));
    QSlider* zygomaticBoneSlider = createSlider();
    QLineEdit* zygomaticBoneEdit = createEdit();
    bindSliderEditor(zygomaticBoneSlider, zygomaticBoneEdit, &zygomaticBone_);
    QHBoxLayout* zygomaticBoneHbox = new QHBoxLayout;
    zygomaticBoneHbox->setSpacing(KSpacing);
    zygomaticBoneHbox->addWidget(zygomaticBoneLabel);
    zygomaticBoneHbox->addWidget(zygomaticBoneSlider);
    zygomaticBoneHbox->addWidget(zygomaticBoneEdit);
    vbox->addItem(zygomaticBoneHbox);
    {
        QLabel* zygomaticBoneLeftLabel = createFixWidthMarqueeLabel(tr("Zygomatic Bone Left"));
        QSlider* zygomaticBoneLeftSlider = createSlider();
        QLineEdit* zygomaticBoneLeftEdit = createEdit();
	    bindSliderEditor(zygomaticBoneLeftSlider, zygomaticBoneLeftEdit, &zygomaticBoneLeft_);
        QHBoxLayout* zygomaticBoneLeftHbox = new QHBoxLayout;
        zygomaticBoneLeftHbox->setSpacing(KSpacing);
        zygomaticBoneLeftHbox->addWidget(zygomaticBoneLeftLabel);
        zygomaticBoneLeftHbox->addWidget(zygomaticBoneLeftSlider);
        zygomaticBoneLeftHbox->addWidget(zygomaticBoneLeftEdit);
        vbox->addItem(zygomaticBoneLeftHbox);
        
        QLabel* zygomaticBoneRightLabel = createFixWidthMarqueeLabel(tr("Zygomatic Bone Right"));
        QSlider* zygomaticBoneRightSlider = createSlider();
        QLineEdit* zygomaticBoneRightEdit = createEdit();
	    bindSliderEditor(zygomaticBoneRightSlider, zygomaticBoneRightEdit, &zygomaticBoneRight_);
        QHBoxLayout* zygomaticBoneRightHbox = new QHBoxLayout;
        zygomaticBoneRightHbox->setSpacing(KSpacing);
        zygomaticBoneRightHbox->addWidget(zygomaticBoneRightLabel);
        zygomaticBoneRightHbox->addWidget(zygomaticBoneRightSlider);
        zygomaticBoneRightHbox->addWidget(zygomaticBoneRightEdit);
        vbox->addItem(zygomaticBoneRightHbox);
    }
    
    QLabel* mandibleLabel = createFixWidthMarqueeLabel(tr("Mandible"));
    QSlider* mandibleSlider = createSlider();
    QLineEdit* mandibleEdit = createEdit();
    bindSliderEditor(mandibleSlider, mandibleEdit, &mandible_);
    QHBoxLayout* mandibleHbox = new QHBoxLayout;
    mandibleHbox->setSpacing(KSpacing);
    mandibleHbox->addWidget(mandibleLabel);
    mandibleHbox->addWidget(mandibleSlider);
    mandibleHbox->addWidget(mandibleEdit);
    vbox->addItem(mandibleHbox);
    {
        auto mandibleLeftLabel = createFixWidthMarqueeLabel(tr("Mandible Left"));
        auto mandibleLeftSlider = createSlider();
        auto mandibleLeftEdit = createEdit();
        bindSliderEditor(mandibleLeftSlider, mandibleLeftEdit, &mandibleLeft_);
        auto mandibleLeftHBox = new QHBoxLayout;
        mandibleLeftHBox->setSpacing(KSpacing);
        mandibleLeftHBox->addItem(createFixedHorizontalSpacer(KHeadSpading));
        mandibleLeftHBox->addWidget(mandibleLeftLabel);
        mandibleLeftHBox->addWidget(mandibleLeftSlider);
        mandibleLeftHBox->addWidget(mandibleLeftEdit);
        vbox->addItem(mandibleLeftHBox);
        
        auto mandibleRightLabel = createFixWidthMarqueeLabel(tr("Mandible Right"));
        auto mandibleRightSlider = createSlider();
        auto mandibleRightEdit = createEdit();
        bindSliderEditor(mandibleRightSlider, mandibleRightEdit, &mandibleRight_);
        auto mandibleRightHBox = new QHBoxLayout;
        mandibleRightHBox->setSpacing(KSpacing);
        mandibleRightHBox->addItem(createFixedHorizontalSpacer(KHeadSpading));
        mandibleRightHBox->addWidget(mandibleRightLabel);
        mandibleRightHBox->addWidget(mandibleRightSlider);
        mandibleRightHBox->addWidget(mandibleRightEdit);
        vbox->addItem(mandibleRightHBox);
    }
    
    QLabel* chinLabel = createFixWidthMarqueeLabel(tr("Chin"));
    QSlider* chinSlider = createSlider();
    QLineEdit* chinEdit = createEdit();
    bindSliderEditor(chinSlider, chinEdit, &chin_);
    QHBoxLayout* chinHbox = new QHBoxLayout;
    chinHbox->setSpacing(KSpacing);
    chinHbox->addWidget(chinLabel);
    chinHbox->addWidget(chinSlider);
    chinHbox->addWidget(chinEdit);
    vbox->addItem(chinHbox);
    
    QLabel* pointedChinLabel = createFixWidthMarqueeLabel(tr("Pointed Chin"));
    QSlider* pointedChinSlider = createSlider();
    QLineEdit* pointedChinEdit = createEdit();
    bindSliderEditor(pointedChinSlider, pointedChinEdit, &pointedChin_);
    QHBoxLayout* pointedChinHbox = new QHBoxLayout;
    pointedChinHbox->setSpacing(KSpacing);
    pointedChinHbox->addWidget(pointedChinLabel);
    pointedChinHbox->addWidget(pointedChinSlider);
    pointedChinHbox->addWidget(pointedChinEdit);
    vbox->addItem(pointedChinHbox);
    {
        auto pointedChinLeftLabel = createFixWidthMarqueeLabel(tr("Pointed Chin Left"));
        auto pointedChinLeftSlider = createSlider();
        auto pointedChinLeftEdit = createEdit();
        bindSliderEditor(pointedChinLeftSlider, pointedChinLeftEdit, &pointedChinLeft_);
        auto pointedChinLeftHBox = new QHBoxLayout;
        pointedChinLeftHBox->setSpacing(KSpacing);
        pointedChinLeftHBox->addItem(createFixedHorizontalSpacer(KHeadSpading));
        pointedChinLeftHBox->addWidget(pointedChinLeftLabel);
        pointedChinLeftHBox->addWidget(pointedChinLeftSlider);
        pointedChinLeftHBox->addWidget(pointedChinLeftEdit);
        vbox->addItem(pointedChinLeftHBox);
        
        auto pointedChinRightLabel = createFixWidthMarqueeLabel(tr("Pointed Chin Right"));
        auto pointedChinRightSlider = createSlider();
        auto pointedChinRightEdit = createEdit();
        bindSliderEditor(pointedChinRightSlider, pointedChinRightEdit, &pointedChinRight_);
        auto pointedChinRightHBox = new QHBoxLayout;
        pointedChinRightHBox->setSpacing(KSpacing);
        pointedChinRightHBox->addItem(createFixedHorizontalSpacer(KHeadSpading));
        pointedChinRightHBox->addWidget(pointedChinRightLabel);
        pointedChinRightHBox->addWidget(pointedChinRightSlider);
        pointedChinRightHBox->addWidget(pointedChinRightEdit);
        vbox->addItem(pointedChinRightHBox);
    }
    
    w->setLayout(vbox);
    
    return w;
}

QWidget* SolutionController::createEyeTab() {
    QWidget *w = new QWidget;
    w->setContentsMargins(0, 0, 0, 0);
    QVBoxLayout* vbox = new QVBoxLayout;
    
    QLabel* bigEyeLabel = createFixWidthMarqueeLabel(tr("Big eye"));
    QSlider* bigEyeSlider = createSlider();
    QLineEdit* bigEyeEdit = createEdit();
    bindSliderEditor(bigEyeSlider, bigEyeEdit, &bigEye_);
    QHBoxLayout* bigEyeHbox = new QHBoxLayout;
    bigEyeHbox->setSpacing(KSpacing);
    bigEyeHbox->addWidget(bigEyeLabel);
    bigEyeHbox->addWidget(bigEyeSlider);
    bigEyeHbox->addWidget(bigEyeEdit);
    vbox->addItem(bigEyeHbox);
    {
        auto bigEyeLeftLabel = createFixWidthMarqueeLabel(tr("Big eye Left"));
        auto bigEyeLeftSlider = createSlider();
        auto bigEyeLeftEdit = createEdit();
        bindSliderEditor(bigEyeLeftSlider, bigEyeLeftEdit, &bigEyeLeft_);
        auto bigEyeLeftHBox = new QHBoxLayout;
        bigEyeLeftHBox->setSpacing(KSpacing);
        bigEyeLeftHBox->addItem(createFixedHorizontalSpacer(KHeadSpading));
        bigEyeLeftHBox->addWidget(bigEyeLeftLabel);
        bigEyeLeftHBox->addWidget(bigEyeLeftSlider);
        bigEyeLeftHBox->addWidget(bigEyeLeftEdit);
        vbox->addItem(bigEyeLeftHBox);
        
        auto bigEyeRightLabel = createFixWidthMarqueeLabel(tr("Big eye Right"));
        auto bigEyeRightSlider = createSlider();
        auto bigEyeRightEdit = createEdit();
        bindSliderEditor(bigEyeRightSlider, bigEyeRightEdit, &bigEyeRight_);
        auto bigEyeRightHBox = new QHBoxLayout;
        bigEyeRightHBox->setSpacing(KSpacing);
        bigEyeRightHBox->addItem(createFixedHorizontalSpacer(KHeadSpading));
        bigEyeRightHBox->addWidget(bigEyeRightLabel);
        bigEyeRightHBox->addWidget(bigEyeRightSlider);
        bigEyeRightHBox->addWidget(bigEyeRightEdit);
        vbox->addItem(bigEyeRightHBox);
    }
    
    QLabel* eyeHeightLabel = createFixWidthMarqueeLabel(tr("Eye height"));
    QSlider* eyeHeightSlider = createSlider();
    QLineEdit* eyeHeightEdit = createEdit();
    bindSliderEditor(eyeHeightSlider, eyeHeightEdit, &eyeHeight_);
    QHBoxLayout* eyeHeightHbox = new QHBoxLayout;
    eyeHeightHbox->setSpacing(KSpacing);
    eyeHeightHbox->addWidget(eyeHeightLabel);
    eyeHeightHbox->addWidget(eyeHeightSlider);
    eyeHeightHbox->addWidget(eyeHeightEdit);
    vbox->addItem(eyeHeightHbox);
    {
        auto eyeHeightLeftLabel = createFixWidthMarqueeLabel(tr("Eye height Left"));
        auto eyeHeightLeftSlider = createSlider();
        auto eyeHeightLeftEdit = createEdit();
        bindSliderEditor(eyeHeightLeftSlider, eyeHeightLeftEdit, &eyeHeightLeft_);
        auto eyeHeightLeftHBox = new QHBoxLayout;
        eyeHeightLeftHBox->setSpacing(KSpacing);
        eyeHeightLeftHBox->addItem(createFixedHorizontalSpacer(KHeadSpading));
        eyeHeightLeftHBox->addWidget(eyeHeightLeftLabel);
        eyeHeightLeftHBox->addWidget(eyeHeightLeftSlider);
        eyeHeightLeftHBox->addWidget(eyeHeightLeftEdit);
        vbox->addItem(eyeHeightLeftHBox);
        
        auto eyeHeightRightLabel = createFixWidthMarqueeLabel(tr("Eye height Right"));
        auto eyeHeightRightSlider = createSlider();
        auto eyeHeightRightEdit = createEdit();
        bindSliderEditor(eyeHeightRightSlider, eyeHeightRightEdit, &eyeHeightRight_);
        auto eyeHeightRightHBox = new QHBoxLayout;
        eyeHeightRightHBox->setSpacing(KSpacing);
        eyeHeightRightHBox->addItem(createFixedHorizontalSpacer(KHeadSpading));
        eyeHeightRightHBox->addWidget(eyeHeightRightLabel);
        eyeHeightRightHBox->addWidget(eyeHeightRightSlider);
        eyeHeightRightHBox->addWidget(eyeHeightRightEdit);
        vbox->addItem(eyeHeightRightHBox);
    }
    
    QLabel* eyeWidthLabel = createFixWidthMarqueeLabel(tr("Eye width"));
    QSlider* eyeWidthSlider = createSlider();
    QLineEdit* eyeWidthEdit = createEdit();
    bindSliderEditor(eyeWidthSlider, eyeWidthEdit, &eyeWidth_);
    QHBoxLayout* eyeWidthHbox = new QHBoxLayout;
    eyeWidthHbox->setSpacing(KSpacing);
    eyeWidthHbox->addWidget(eyeWidthLabel);
    eyeWidthHbox->addWidget(eyeWidthSlider);
    eyeWidthHbox->addWidget(eyeWidthEdit);
    vbox->addItem(eyeWidthHbox);
    {
        auto eyeWidthLeftLabel = createFixWidthMarqueeLabel(tr("Eye width Left"));
        auto eyeWidthLeftSlider = createSlider();
        auto eyeWidthLeftEdit = createEdit();
        bindSliderEditor(eyeWidthLeftSlider, eyeWidthLeftEdit, &eyeWidthLeft_);
        auto eyeWidthLeftHBox = new QHBoxLayout;
        eyeWidthLeftHBox->setSpacing(KSpacing);
        eyeWidthLeftHBox->addItem(createFixedHorizontalSpacer(KHeadSpading));
        eyeWidthLeftHBox->addWidget(eyeWidthLeftLabel);
        eyeWidthLeftHBox->addWidget(eyeWidthLeftSlider);
        eyeWidthLeftHBox->addWidget(eyeWidthLeftEdit);
        vbox->addItem(eyeWidthLeftHBox);
        
        auto eyeWidthRightLabel = createFixWidthMarqueeLabel(tr("Eye width Right"));
        auto eyeWidthRightSlider = createSlider();
        auto eyeWidthRightEdit = createEdit();
        bindSliderEditor(eyeWidthRightSlider, eyeWidthRightEdit, &eyeWidthRight_);
        auto eyeWidthRightHBox = new QHBoxLayout;
        eyeWidthRightHBox->setSpacing(KSpacing);
        eyeWidthRightHBox->addItem(createFixedHorizontalSpacer(KHeadSpading));
        eyeWidthRightHBox->addWidget(eyeWidthRightLabel);
        eyeWidthRightHBox->addWidget(eyeWidthRightSlider);
        eyeWidthRightHBox->addWidget(eyeWidthRightEdit);
        vbox->addItem(eyeWidthRightHBox);
    }
    
    QLabel* eyeDistanceLabel = createFixWidthMarqueeLabel(tr("Eye distance"));
    QSlider* eyeDistanceSlider = createSlider();
    QLineEdit* eyeDistanceEdit = createEdit();
    bindSliderEditor(eyeDistanceSlider, eyeDistanceEdit, &eyeDistance_);
    QHBoxLayout* eyeDistanceHbox = new QHBoxLayout;
    eyeDistanceHbox->setSpacing(KSpacing);
    eyeDistanceHbox->addWidget(eyeDistanceLabel);
    eyeDistanceHbox->addWidget(eyeDistanceSlider);
    eyeDistanceHbox->addWidget(eyeDistanceEdit);
    vbox->addItem(eyeDistanceHbox);
    {
        auto eyeDistanceLeftLabel = createFixWidthMarqueeLabel(tr("Eye distance Left"));
        auto eyeDistanceLeftSlider = createSlider();
        auto eyeDistanceLeftEdit = createEdit();
        bindSliderEditor(eyeDistanceLeftSlider, eyeDistanceLeftEdit, &eyeDistanceLeft_);
        auto eyeDistanceLeftHBox = new QHBoxLayout;
        eyeDistanceLeftHBox->setSpacing(KSpacing);
        eyeDistanceLeftHBox->addItem(createFixedHorizontalSpacer(KHeadSpading));
        eyeDistanceLeftHBox->addWidget(eyeDistanceLeftLabel);
        eyeDistanceLeftHBox->addWidget(eyeDistanceLeftSlider);
        eyeDistanceLeftHBox->addWidget(eyeDistanceLeftEdit);
        vbox->addItem(eyeDistanceLeftHBox);
        
        auto eyeDistanceRightLabel = createFixWidthMarqueeLabel(tr("Eye distance Right"));
        auto eyeDistanceRightSlider = createSlider();
        auto eyeDistanceRightEdit = createEdit();
        bindSliderEditor(eyeDistanceRightSlider, eyeDistanceRightEdit, &eyeDistanceRight_);
        auto eyeDistanceRightHBox = new QHBoxLayout;
        eyeDistanceRightHBox->setSpacing(KSpacing);
        eyeDistanceRightHBox->addItem(createFixedHorizontalSpacer(KHeadSpading));
        eyeDistanceRightHBox->addWidget(eyeDistanceRightLabel);
        eyeDistanceRightHBox->addWidget(eyeDistanceRightSlider);
        eyeDistanceRightHBox->addWidget(eyeDistanceRightEdit);
        vbox->addItem(eyeDistanceRightHBox);
    }
    
    QLabel* eyeCornerLabel = createFixWidthMarqueeLabel(tr("Eye corner"));
    QSlider* eyeCornerSlider = createSlider();
    QLineEdit* eyeCornerEdit = createEdit();
    bindSliderEditor(eyeCornerSlider, eyeCornerEdit, &eyeCorner_);
    QHBoxLayout* eyeCornerHbox = new QHBoxLayout;
    eyeCornerHbox->setSpacing(KSpacing);
    eyeCornerHbox->addWidget(eyeCornerLabel);
    eyeCornerHbox->addWidget(eyeCornerSlider);
    eyeCornerHbox->addWidget(eyeCornerEdit);
    vbox->addItem(eyeCornerHbox);
    {
        auto eyeCornerLeftLabel = createFixWidthMarqueeLabel(tr("Eye corner Left"));
        auto eyeCornerLeftSlider = createSlider();
        auto eyeCornerLeftEdit = createEdit();
        bindSliderEditor(eyeCornerLeftSlider, eyeCornerLeftEdit, &eyeCornerLeft_);
        auto eyeCornerLeftHBox = new QHBoxLayout;
        eyeCornerLeftHBox->setSpacing(KSpacing);
        eyeCornerLeftHBox->addItem(createFixedHorizontalSpacer(KHeadSpading));
        eyeCornerLeftHBox->addWidget(eyeCornerLeftLabel);
        eyeCornerLeftHBox->addWidget(eyeCornerLeftSlider);
        eyeCornerLeftHBox->addWidget(eyeCornerLeftEdit);
        vbox->addItem(eyeCornerLeftHBox);
        
        auto eyeCornerRightLabel = createFixWidthMarqueeLabel(tr("Eye corner Right"));
        auto eyeCornerRightSlider = createSlider();
        auto eyeCornerRightEdit = createEdit();
        bindSliderEditor(eyeCornerRightSlider, eyeCornerRightEdit, &eyeCornerRight_);
        auto eyeCornerRightHBox = new QHBoxLayout;
        eyeCornerRightHBox->setSpacing(KSpacing);
        eyeCornerRightHBox->addItem(createFixedHorizontalSpacer(KHeadSpading));
        eyeCornerRightHBox->addWidget(eyeCornerRightLabel);
        eyeCornerRightHBox->addWidget(eyeCornerRightSlider);
        eyeCornerRightHBox->addWidget(eyeCornerRightEdit);
        vbox->addItem(eyeCornerRightHBox);
    }
    
    QLabel* eyePosLabel = createFixWidthMarqueeLabel(tr("Eye pos"));
    QSlider* eyePosSlider = createSlider();
    QLineEdit* eyePosEdit = createEdit();
    bindSliderEditor(eyePosSlider, eyePosEdit, &eyePos_);
    QHBoxLayout* eyePosHbox = new QHBoxLayout;
    eyePosHbox->setSpacing(KSpacing);
    eyePosHbox->addWidget(eyePosLabel);
    eyePosHbox->addWidget(eyePosSlider);
    eyePosHbox->addWidget(eyePosEdit);
    vbox->addItem(eyePosHbox);
    {
        auto eyePosLeftLabel = createFixWidthMarqueeLabel(tr("Eye pos Left"));
        auto eyePosLeftSlider = createSlider();
        auto eyePosLeftEdit = createEdit();
        bindSliderEditor(eyePosLeftSlider, eyePosLeftEdit, &eyePosLeft_);
        auto eyePosLeftHBox = new QHBoxLayout;
        eyePosLeftHBox->setSpacing(KSpacing);
        eyePosLeftHBox->addItem(createFixedHorizontalSpacer(KHeadSpading));
        eyePosLeftHBox->addWidget(eyePosLeftLabel);
        eyePosLeftHBox->addWidget(eyePosLeftSlider);
        eyePosLeftHBox->addWidget(eyePosLeftEdit);
        vbox->addItem(eyePosLeftHBox);
        
        auto eyePosRightLabel = createFixWidthMarqueeLabel(tr("Eye pos Right"));
        auto eyePosRightSlider = createSlider();
        auto eyePosRightEdit = createEdit();
        bindSliderEditor(eyePosRightSlider, eyePosRightEdit, &eyePosRight_);
        auto eyePosRightHBox = new QHBoxLayout;
        eyePosRightHBox->setSpacing(KSpacing);
        eyePosRightHBox->addItem(createFixedHorizontalSpacer(KHeadSpading));
        eyePosRightHBox->addWidget(eyePosRightLabel);
        eyePosRightHBox->addWidget(eyePosRightSlider);
        eyePosRightHBox->addWidget(eyePosRightEdit);
        vbox->addItem(eyePosRightHBox);
    }
    
    QSpacerItem* spacer = new QSpacerItem(20, 20, QSizePolicy::Fixed, QSizePolicy::Expanding);
    
    vbox->addItem(spacer);
    
    w->setLayout(vbox);
    return w;
}

QWidget* SolutionController::createNoseTab() {
    QWidget *w = new QWidget;
    w->setContentsMargins(0, 0, 0, 0);
    QVBoxLayout* vbox = new QVBoxLayout;
    
    QLabel* slimNoseLabel = createFixWidthMarqueeLabel(tr("Slim nose"));
    QSlider* slimNoseSlider = createSlider();
    QLineEdit* slimNoseEdit = createEdit();
    bindSliderEditor(slimNoseSlider, slimNoseEdit, &slimNose_);
    QHBoxLayout* slimNoseHbox = new QHBoxLayout;
    slimNoseHbox->setSpacing(KSpacing);
    slimNoseHbox->addWidget(slimNoseLabel);
    slimNoseHbox->addWidget(slimNoseSlider);
    slimNoseHbox->addWidget(slimNoseEdit);
    vbox->addItem(slimNoseHbox);
    
    QLabel* slimNostrilLabel = createFixWidthMarqueeLabel(tr("Slim nostril"));
    QSlider* slimNostrilSlider = createSlider();
    QLineEdit* slimNostrilEdit = createEdit();
    bindSliderEditor(slimNostrilSlider, slimNostrilEdit, &slimNostril_);
    QHBoxLayout* slimNostrilHbox = new QHBoxLayout;
    slimNostrilHbox->setSpacing(KSpacing);
    slimNostrilHbox->addWidget(slimNostrilLabel);
    slimNostrilHbox->addWidget(slimNostrilSlider);
    slimNostrilHbox->addWidget(slimNostrilEdit);
    vbox->addItem(slimNostrilHbox);
    {
        auto slimNostrilLeftLabel = createFixWidthMarqueeLabel(tr("Slim nostril Left"));
        auto slimNostrilLeftSlider = createSlider();
        auto slimNostrilLeftEdit = createEdit();
        bindSliderEditor(slimNostrilLeftSlider, slimNostrilLeftEdit, &slimNostrilLeft_);
        auto slimNostrilLeftHBox = new QHBoxLayout;
        slimNostrilLeftHBox->setSpacing(KSpacing);
        slimNostrilLeftHBox->addItem(createFixedHorizontalSpacer(KHeadSpading));
        slimNostrilLeftHBox->addWidget(slimNostrilLeftLabel);
        slimNostrilLeftHBox->addWidget(slimNostrilLeftSlider);
        slimNostrilLeftHBox->addWidget(slimNostrilLeftEdit);
        vbox->addItem(slimNostrilLeftHBox);
        
        auto slimNostrilRightLabel = createFixWidthMarqueeLabel(tr("Slim nostril Right"));
        auto slimNostrilRightSlider = createSlider();
        auto slimNostrilRightEdit = createEdit();
        bindSliderEditor(slimNostrilRightSlider, slimNostrilRightEdit, &slimNostrilRight_);
        auto slimNostrilRightHBox = new QHBoxLayout;
        slimNostrilRightHBox->setSpacing(KSpacing);
        slimNostrilRightHBox->addItem(createFixedHorizontalSpacer(KHeadSpading));
        slimNostrilRightHBox->addWidget(slimNostrilRightLabel);
        slimNostrilRightHBox->addWidget(slimNostrilRightSlider);
        slimNostrilRightHBox->addWidget(slimNostrilRightEdit);
        vbox->addItem(slimNostrilRightHBox);
    }
    
    QLabel* slimNoseBridgeLabel = createFixWidthMarqueeLabel(tr("Slim nose bridge"));
    QSlider* slimNoseBridgeSlider = createSlider();
    QLineEdit* slimNoseBridgeEdit = createEdit();
    bindSliderEditor(slimNoseBridgeSlider, slimNoseBridgeEdit, &slimNoseBridge_);
    QHBoxLayout* slimNoseBridgeHbox = new QHBoxLayout;
    slimNoseBridgeHbox->setSpacing(KSpacing);
    slimNoseBridgeHbox->addWidget(slimNoseBridgeLabel);
    slimNoseBridgeHbox->addWidget(slimNoseBridgeSlider);
    slimNoseBridgeHbox->addWidget(slimNoseBridgeEdit);
    vbox->addItem(slimNoseBridgeHbox);
    
    QLabel* straightenNoseLabel = createFixWidthMarqueeLabel(tr("Straighten nose"));
    QSlider* straightenNoseSlider = createSlider();
    QLineEdit* straightenNoseEdit = createEdit();
    bindSliderEditor(straightenNoseSlider, straightenNoseEdit, &straightenNose_);
    QHBoxLayout* straightenNoseHbox = new QHBoxLayout;
    straightenNoseHbox->setSpacing(KSpacing);
    straightenNoseHbox->addWidget(straightenNoseLabel);
    straightenNoseHbox->addWidget(straightenNoseSlider);
    straightenNoseHbox->addWidget(straightenNoseEdit);
    vbox->addItem(straightenNoseHbox);
    
    QLabel* nosePosLabel = createFixWidthMarqueeLabel(tr("Nose pos"));
    QSlider* nosePosSlider = createSlider();
    QLineEdit* nosePosEdit = createEdit();
    bindSliderEditor(nosePosSlider, nosePosEdit, &nosePos_);
    QHBoxLayout* nosePosHbox = new QHBoxLayout;
    nosePosHbox->setSpacing(KSpacing);
    nosePosHbox->addWidget(nosePosLabel);
    nosePosHbox->addWidget(nosePosSlider);
    nosePosHbox->addWidget(nosePosEdit);
    vbox->addItem(nosePosHbox);
    
    QSpacerItem* spacer = new QSpacerItem(20, 20, QSizePolicy::Fixed, QSizePolicy::Expanding);
    
    vbox->addItem(spacer);
    
    w->setLayout(vbox);
    return w;
}

QWidget* SolutionController::createMouthTab() {
    QWidget *w = new QWidget;
    w->setContentsMargins(0, 0, 0, 0);
    
    QLabel* mouthLabel = createFixWidthMarqueeLabel(tr("Mouth"));
    QSlider* mouthSlider = createSlider();
    QLineEdit* mouthEdit = createEdit();
    bindSliderEditor(mouthSlider, mouthEdit, &mouth_);
    QHBoxLayout* mouthHbox = new QHBoxLayout;
    mouthHbox->setSpacing(KSpacing);
    mouthHbox->addWidget(mouthLabel);
    mouthHbox->addWidget(mouthSlider);
    mouthHbox->addWidget(mouthEdit);
    
    QLabel* mouthTiltLabel = createFixWidthMarqueeLabel(tr("Mouth tilt"));
    QSlider* mouthTiltSlider = createSlider();
    QLineEdit* mouthTiltEdit = createEdit();
    bindSliderEditor(mouthTiltSlider, mouthTiltEdit, &mouthTilt_);
    QHBoxLayout* mouthTiltHbox = new QHBoxLayout;
    mouthTiltHbox->setSpacing(KSpacing);
    mouthTiltHbox->addWidget(mouthTiltLabel);
    mouthTiltHbox->addWidget(mouthTiltSlider);
    mouthTiltHbox->addWidget(mouthTiltEdit);
    
    QLabel* philtrumLabel = createFixWidthMarqueeLabel(tr("Philtrum"));
    QSlider* philtrumSlider = createSlider();
    QLineEdit* philtrumEdit = createEdit();
    bindSliderEditor(philtrumSlider, philtrumEdit, &philtrum_);
    QHBoxLayout* philtrumHbox = new QHBoxLayout;
    philtrumHbox->setSpacing(KSpacing);
    philtrumHbox->addWidget(philtrumLabel);
    philtrumHbox->addWidget(philtrumSlider);
    philtrumHbox->addWidget(philtrumEdit);
    
    QLabel* mouthPosLabel = createFixWidthMarqueeLabel(tr("Mouth pos"));
    QSlider* mouthPosSlider = createSlider();
    QLineEdit* mouthPosEdit = createEdit();
    bindSliderEditor(mouthPosSlider, mouthPosEdit, &mouthPos_);
    QHBoxLayout* mouthPosHbox = new QHBoxLayout;
    mouthPosHbox->setSpacing(KSpacing);
    mouthPosHbox->addWidget(mouthPosLabel);
    mouthPosHbox->addWidget(mouthPosSlider);
    mouthPosHbox->addWidget(mouthPosEdit);
    
    QSpacerItem* spacer = new QSpacerItem(20, 20, QSizePolicy::Fixed, QSizePolicy::Expanding);
    
    QVBoxLayout* vbox = new QVBoxLayout;
    vbox->addItem(mouthHbox);
    vbox->addItem(mouthTiltHbox);
    vbox->addItem(philtrumHbox);
    vbox->addItem(mouthPosHbox);
    vbox->addItem(spacer);
    
    w->setLayout(vbox);
    return w;
}

QWidget* SolutionController::createEyebrowTab() {
    QWidget *w = new QWidget;
    w->setContentsMargins(0, 0, 0, 0);
    QVBoxLayout* vbox = new QVBoxLayout;
    
    QLabel* eyebrowThicknessLabel = createFixWidthMarqueeLabel(tr("Eyebrow thickness"));
    QSlider* eyebrowThicknessSlider = createSlider();
    QLineEdit* eyebrowThicknessEdit = createEdit();
    bindSliderEditor(eyebrowThicknessSlider, eyebrowThicknessEdit, &eyebrowThickness_);
    QHBoxLayout* eyebrowThicknessHbox = new QHBoxLayout;
    eyebrowThicknessHbox->setSpacing(KSpacing);
    eyebrowThicknessHbox->addWidget(eyebrowThicknessLabel);
    eyebrowThicknessHbox->addWidget(eyebrowThicknessSlider);
    eyebrowThicknessHbox->addWidget(eyebrowThicknessEdit);
    vbox->addItem(eyebrowThicknessHbox);
    {
        auto eyebrowThicknessLeftLabel = createFixWidthMarqueeLabel(tr("Eyebrow thickness Left"));
        auto eyebrowThicknessLeftSlider = createSlider();
        auto eyebrowThicknessLeftEdit = createEdit();
        bindSliderEditor(eyebrowThicknessLeftSlider, eyebrowThicknessLeftEdit, &eyebrowThicknessLeft_);
        auto eyebrowThicknessLeftHBox = new QHBoxLayout;
        eyebrowThicknessLeftHBox->setSpacing(KSpacing);
        eyebrowThicknessLeftHBox->addItem(createFixedHorizontalSpacer(KHeadSpading));
        eyebrowThicknessLeftHBox->addWidget(eyebrowThicknessLeftLabel);
        eyebrowThicknessLeftHBox->addWidget(eyebrowThicknessLeftSlider);
        eyebrowThicknessLeftHBox->addWidget(eyebrowThicknessLeftEdit);
        vbox->addItem(eyebrowThicknessLeftHBox);
        
        auto eyebrowThicknessRightLabel = createFixWidthMarqueeLabel(tr("Eyebrow thickness Right"));
        auto eyebrowThicknessRightSlider = createSlider();
        auto eyebrowThicknessRightEdit = createEdit();
        bindSliderEditor(eyebrowThicknessRightSlider, eyebrowThicknessRightEdit, &eyebrowThicknessRight_);
        auto eyebrowThicknessRightHBox = new QHBoxLayout;
        eyebrowThicknessRightHBox->setSpacing(KSpacing);
        eyebrowThicknessRightHBox->addItem(createFixedHorizontalSpacer(KHeadSpading));
        eyebrowThicknessRightHBox->addWidget(eyebrowThicknessRightLabel);
        eyebrowThicknessRightHBox->addWidget(eyebrowThicknessRightSlider);
        eyebrowThicknessRightHBox->addWidget(eyebrowThicknessRightEdit);
        vbox->addItem(eyebrowThicknessRightHBox);
    }
    
    QLabel* eyebrowDistanceLabel = createFixWidthMarqueeLabel(tr("Eyebrow distance"));
    QSlider* eyebrowDistanceSlider = createSlider();
    QLineEdit* eyebrowDistanceEdit = createEdit();
    bindSliderEditor(eyebrowDistanceSlider, eyebrowDistanceEdit, &eyebrowDistance_);
    QHBoxLayout* eyebrowDistanceHbox = new QHBoxLayout;
    eyebrowDistanceHbox->setSpacing(KSpacing);
    eyebrowDistanceHbox->addWidget(eyebrowDistanceLabel);
    eyebrowDistanceHbox->addWidget(eyebrowDistanceSlider);
    eyebrowDistanceHbox->addWidget(eyebrowDistanceEdit);
    vbox->addItem(eyebrowDistanceHbox);
    {
        auto eyebrowDistanceLeftLabel = createFixWidthMarqueeLabel(tr("Eyebrow distance Left"));
        auto eyebrowDistanceLeftSlider = createSlider();
        auto eyebrowDistanceLeftEdit = createEdit();
        bindSliderEditor(eyebrowDistanceLeftSlider, eyebrowDistanceLeftEdit, &eyebrowDistanceLeft_);
        auto eyebrowDistanceLeftHBox = new QHBoxLayout;
        eyebrowDistanceLeftHBox->setSpacing(KSpacing);
        eyebrowDistanceLeftHBox->addItem(createFixedHorizontalSpacer(KHeadSpading));
        eyebrowDistanceLeftHBox->addWidget(eyebrowDistanceLeftLabel);
        eyebrowDistanceLeftHBox->addWidget(eyebrowDistanceLeftSlider);
        eyebrowDistanceLeftHBox->addWidget(eyebrowDistanceLeftEdit);
        vbox->addItem(eyebrowDistanceLeftHBox);
        
        auto eyebrowDistanceRightLabel = createFixWidthMarqueeLabel(tr("Eyebrow distance Right"));
        auto eyebrowDistanceRightSlider = createSlider();
        auto eyebrowDistanceRightEdit = createEdit();
        bindSliderEditor(eyebrowDistanceRightSlider, eyebrowDistanceRightEdit, &eyebrowDistanceRight_);
        auto eyebrowDistanceRightHBox = new QHBoxLayout;
        eyebrowDistanceRightHBox->setSpacing(KSpacing);
        eyebrowDistanceRightHBox->addItem(createFixedHorizontalSpacer(KHeadSpading));
        eyebrowDistanceRightHBox->addWidget(eyebrowDistanceRightLabel);
        eyebrowDistanceRightHBox->addWidget(eyebrowDistanceRightSlider);
        eyebrowDistanceRightHBox->addWidget(eyebrowDistanceRightEdit);
        vbox->addItem(eyebrowDistanceRightHBox);
    }
    
    QLabel* eyebrowTiltLabel = createFixWidthMarqueeLabel(tr("Eyebrow tilt"));
    QSlider* eyebrowTiltSlider = createSlider();
    QLineEdit* eyebrowTiltEdit = createEdit();
    bindSliderEditor(eyebrowTiltSlider, eyebrowTiltEdit, &eyebrowTilt_);
    QHBoxLayout* eyebrowTiltHbox = new QHBoxLayout;
    eyebrowTiltHbox->setSpacing(KSpacing);
    eyebrowTiltHbox->addWidget(eyebrowTiltLabel);
    eyebrowTiltHbox->addWidget(eyebrowTiltSlider);
    eyebrowTiltHbox->addWidget(eyebrowTiltEdit);
    vbox->addItem(eyebrowTiltHbox);
    {
        auto eyebrowTiltLeftLabel = createFixWidthMarqueeLabel(tr("Eyebrow tilt Left"));
        auto eyebrowTiltLeftSlider = createSlider();
        auto eyebrowTiltLeftEdit = createEdit();
        bindSliderEditor(eyebrowTiltLeftSlider, eyebrowTiltLeftEdit, &eyebrowTiltLeft_);
        auto eyebrowTiltLeftHBox = new QHBoxLayout;
        eyebrowTiltLeftHBox->setSpacing(KSpacing);
        eyebrowTiltLeftHBox->addItem(createFixedHorizontalSpacer(KHeadSpading));
        eyebrowTiltLeftHBox->addWidget(eyebrowTiltLeftLabel);
        eyebrowTiltLeftHBox->addWidget(eyebrowTiltLeftSlider);
        eyebrowTiltLeftHBox->addWidget(eyebrowTiltLeftEdit);
        vbox->addItem(eyebrowTiltLeftHBox);
        
        auto eyebrowTiltRightLabel = createFixWidthMarqueeLabel(tr("Eyebrow tilt Right"));
        auto eyebrowTiltRightSlider = createSlider();
        auto eyebrowTiltRightEdit = createEdit();
        bindSliderEditor(eyebrowTiltRightSlider, eyebrowTiltRightEdit, &eyebrowTiltRight_);
        auto eyebrowTiltRightHBox = new QHBoxLayout;
        eyebrowTiltRightHBox->setSpacing(KSpacing);
        eyebrowTiltRightHBox->addItem(createFixedHorizontalSpacer(KHeadSpading));
        eyebrowTiltRightHBox->addWidget(eyebrowTiltRightLabel);
        eyebrowTiltRightHBox->addWidget(eyebrowTiltRightSlider);
        eyebrowTiltRightHBox->addWidget(eyebrowTiltRightEdit);
        vbox->addItem(eyebrowTiltRightHBox);
    }
    
    QLabel* eyebrowPosLabel = createFixWidthMarqueeLabel(tr("Eyebrow pos"));
    QSlider* eyebrowPosSlider = createSlider();
    QLineEdit* eyebrowPosEdit = createEdit();
    bindSliderEditor(eyebrowPosSlider, eyebrowPosEdit, &eyebrowPos_);
    QHBoxLayout* eyebrowPosHbox = new QHBoxLayout;
    eyebrowPosHbox->setSpacing(KSpacing);
    eyebrowPosHbox->addWidget(eyebrowPosLabel);
    eyebrowPosHbox->addWidget(eyebrowPosSlider);
    eyebrowPosHbox->addWidget(eyebrowPosEdit);
    vbox->addItem(eyebrowPosHbox);
    {
        auto eyebrowPosLeftLabel = createFixWidthMarqueeLabel(tr("Eyebrow pos Left"));
        auto eyebrowPosLeftSlider = createSlider();
        auto eyebrowPosLeftEdit = createEdit();
        bindSliderEditor(eyebrowPosLeftSlider, eyebrowPosLeftEdit, &eyebrowPosLeft_);
        auto eyebrowPosLeftHBox = new QHBoxLayout;
        eyebrowPosLeftHBox->setSpacing(KSpacing);
        eyebrowPosLeftHBox->addItem(createFixedHorizontalSpacer(KHeadSpading));
        eyebrowPosLeftHBox->addWidget(eyebrowPosLeftLabel);
        eyebrowPosLeftHBox->addWidget(eyebrowPosLeftSlider);
        eyebrowPosLeftHBox->addWidget(eyebrowPosLeftEdit);
        vbox->addItem(eyebrowPosLeftHBox);
        
        auto eyebrowPosRightLabel = createFixWidthMarqueeLabel(tr("Eyebrow pos Right"));
        auto eyebrowPosRightSlider = createSlider();
        auto eyebrowPosRightEdit = createEdit();
        bindSliderEditor(eyebrowPosRightSlider, eyebrowPosRightEdit, &eyebrowPosRight_);
        auto eyebrowPosRightHBox = new QHBoxLayout;
        eyebrowPosRightHBox->setSpacing(KSpacing);
        eyebrowPosRightHBox->addItem(createFixedHorizontalSpacer(KHeadSpading));
        eyebrowPosRightHBox->addWidget(eyebrowPosRightLabel);
        eyebrowPosRightHBox->addWidget(eyebrowPosRightSlider);
        eyebrowPosRightHBox->addWidget(eyebrowPosRightEdit);
        vbox->addItem(eyebrowPosRightHBox);
    }
    
    QSpacerItem* spacer = new QSpacerItem(20, 20, QSizePolicy::Fixed, QSizePolicy::Expanding);
    
    vbox->addItem(spacer);
    
    w->setLayout(vbox);
    return w;
}

QWidget* SolutionController::createOtherTab() {
    QWidget *w = new QWidget;
    w->setContentsMargins(0, 0, 0, 0);
    
    QLabel* foreheadLineLabel = createFixWidthMarqueeLabel(tr("Forehead line"));
    QSlider* foreheadLineSlider = createSlider();
    QLineEdit* foreheadLineEdit = createEdit();
    bindSliderEditor(foreheadLineSlider, foreheadLineEdit, &foreheadLine_);
    QHBoxLayout* foreheadLineHbox = new QHBoxLayout;
    foreheadLineHbox->setSpacing(KSpacing);
    foreheadLineHbox->addWidget(foreheadLineLabel);
    foreheadLineHbox->addWidget(foreheadLineSlider);
    foreheadLineHbox->addWidget(foreheadLineEdit);
    
    QLabel* smileLineLabel = createFixWidthMarqueeLabel(tr("Smile line"));
    QSlider* smileLineSlider = createSlider();
    QLineEdit* smileLineEdit = createEdit();
    bindSliderEditor(smileLineSlider, smileLineEdit, &smileLine_);
    QHBoxLayout* smileLineHbox = new QHBoxLayout;
    smileLineHbox->setSpacing(KSpacing);
    smileLineHbox->addWidget(smileLineLabel);
    smileLineHbox->addWidget(smileLineSlider);
    smileLineHbox->addWidget(smileLineEdit);
    
    QLabel* whitenTeethLabel = createFixWidthMarqueeLabel(tr("Whiten teeth"));
    QSlider* whitenTeethSlider = createSlider();
    QLineEdit* whitenTeethEdit = createEdit();
    bindSliderEditor(whitenTeethSlider, whitenTeethEdit, &whitenTeeth_);
    QHBoxLayout* whitenTeethHbox = new QHBoxLayout;
    whitenTeethHbox->setSpacing(KSpacing);
    whitenTeethHbox->addWidget(whitenTeethLabel);
    whitenTeethHbox->addWidget(whitenTeethSlider);
    whitenTeethHbox->addWidget(whitenTeethEdit);
    
    QLabel* darkCircleLabel = createFixWidthMarqueeLabel(tr("Dark circle"));
    QSlider* darkCircleSlider = createSlider();
    QLineEdit* darkCircleEdit = createEdit();
    bindSliderEditor(darkCircleSlider, darkCircleEdit, &darkCircle_);
    QHBoxLayout* darkCircleHbox = new QHBoxLayout;
    darkCircleHbox->setSpacing(KSpacing);
    darkCircleHbox->addWidget(darkCircleLabel);
    darkCircleHbox->addWidget(darkCircleSlider);
    darkCircleHbox->addWidget(darkCircleEdit);
    
    QLabel* brightenEyeLabel = createFixWidthMarqueeLabel(tr("Brighten eye"));
    QSlider* brightenEyeSlider = createSlider();
    QLineEdit* brightenEyeEdit = createEdit();
    bindSliderEditor(brightenEyeSlider, brightenEyeEdit, &brightenEye_);
    QHBoxLayout* brightenEyeHbox = new QHBoxLayout;
    brightenEyeHbox->setSpacing(KSpacing);
    brightenEyeHbox->addWidget(brightenEyeLabel);
    brightenEyeHbox->addWidget(brightenEyeSlider);
    brightenEyeHbox->addWidget(brightenEyeEdit);
    
    QLabel* midFaceLabel = createFixWidthMarqueeLabel(tr("Mid face"));
    QSlider* midFaceSlider = createSlider();
    QLineEdit* midFaceEdit = createEdit();
    bindSliderEditor(midFaceSlider, midFaceEdit, &midFace_);
    QHBoxLayout* midFaceHbox = new QHBoxLayout;
    midFaceHbox->setSpacing(KSpacing);
    midFaceHbox->addWidget(midFaceLabel);
    midFaceHbox->addWidget(midFaceSlider);
    midFaceHbox->addWidget(midFaceEdit);
    
    QLabel* lowerFaceLabel = createFixWidthMarqueeLabel(tr("Lower face"));
    QSlider* lowerFaceSlider = createSlider();
    QLineEdit* lowerFaceEdit = createEdit();
    bindSliderEditor(lowerFaceSlider, lowerFaceEdit, &lowerFace_);
    QHBoxLayout* lowerFaceHbox = new QHBoxLayout;
    lowerFaceHbox->setSpacing(KSpacing);
    lowerFaceHbox->addWidget(lowerFaceLabel);
    lowerFaceHbox->addWidget(lowerFaceSlider);
    lowerFaceHbox->addWidget(lowerFaceEdit);
    
    QSpacerItem* spacer = new QSpacerItem(20, 20, QSizePolicy::Fixed, QSizePolicy::Expanding);
    
    QVBoxLayout* vbox = new QVBoxLayout;
    vbox->addItem(foreheadLineHbox);
    vbox->addItem(smileLineHbox);
    vbox->addItem(whitenTeethHbox);
    vbox->addItem(darkCircleHbox);
    vbox->addItem(brightenEyeHbox);
    vbox->addItem(midFaceHbox);
    vbox->addItem(lowerFaceHbox);
    vbox->addItem(spacer);
    
    w->setLayout(vbox);
    return w;
}

void SolutionController::refreshEnableSkinState() {
    whitenLabel_->setEnabled(enableSkin_);
    whitenSlider_->setEnabled(enableSkin_);
    whitenEdit_->setEnabled(enableSkin_);
    clarityLabel_->setEnabled(enableSkin_);
    claritySlider_->setEnabled(enableSkin_);
    clarityEdit_->setEnabled(enableSkin_);
    smoothingLabel_->setEnabled(enableSkin_);
    smoothingSlider_->setEnabled(enableSkin_);
    smoothingEdit_->setEnabled(enableSkin_);
    
}

void SolutionController::refreshEnableFaceShapingState() {
    tabBodyShaping_->setEnabled(enableFaceShaping_);
}

BeautyEnhancementController::BeautyEnhancementController(GlobalModel* app, QWidget* parent/* = nullptr*/)
: CiciWidget(parent), app_(app) {
    assert(app_ != nullptr);
}

void BeautyEnhancementController::createUI() {
    QLabel* camera = createLabel(tr("Camera:"));
    QLabel* cameraName = createVideoInputLabel(app_, tr("No Camera"));
    
    QHBoxLayout* hbox = new QHBoxLayout;
    hbox->addWidget(camera);
    hbox->addWidget(cameraName);
    hbox->addStretch();
    
    enableBeautyCheckBox_ = createCheckBox(tr("Enable beauty"));
    
    QHBoxLayout* hbox1 = new QHBoxLayout;
    hbox1->addWidget(enableBeautyCheckBox_);
    hbox1->addStretch();
    
    tabWidget_ = new QTabWidget;
    applyDisabledTabStyle(tabWidget_);
    tabWidget_->setTabsClosable(true);
    QVBoxLayout* vbox = new QVBoxLayout;
    vbox->addItem(hbox);
    vbox->addItem(hbox1);
    vbox->addWidget(tabWidget_);
    
    addScrollBar(vbox);
    
    newSlnWidget_ = new QWidget;
    tabWidget_->addTab(newSlnWidget_, tr("+ New Sln"));
    refreshNewSolutionTabCloseButton();
    
    QObject::connect(enableBeautyCheckBox_, &QCheckBox::stateChanged, this, [this](int state) {
        const auto enableBeauty = (state == Qt::Checked);
        refreshEnableState(enableBeauty);
        if (!loadingConfig_) {
            emit settingPatchRequested(JsonUtility::Patch::add(u8"/beauty/enableBeauty", enableBeauty));
        }
    });
    QObject::connect(enableBeautyCheckBox_, &QCheckBox::clicked, this, [this](bool checked) {
        reportBeautyEnhancementEnableToggle(app_, checked);
    });
    QObject::connect(tabWidget_, &QTabWidget::tabCloseRequested, this, &BeautyEnhancementController::onTabCloseRequested);
    QObject::connect(tabWidget_, &QTabWidget::tabBarClicked, this, &BeautyEnhancementController::onTabBarClicked);
    QObject::connect(tabWidget_, &QTabWidget::currentChanged, this, &BeautyEnhancementController::onCurrentChanged);
    QObject::connect(tabWidget_, &QTabWidget::tabBarDoubleClicked, this, &BeautyEnhancementController::onTabBarDoubleClicked);
}

BeautyEnhancementController::~BeautyEnhancementController() {
}

void BeautyEnhancementController::switchLanguage() {
}

void BeautyEnhancementController::refreshEnableState(bool b) {
    tabWidget_->setEnabled(b);
}

bool BeautyEnhancementController::isSolutionTab(int index) const {
    return index >= 0 &&
           index < tabWidget_->count() &&
           tabWidget_->widget(index) != newSlnWidget_;
}

int BeautyEnhancementController::solutionTabCount() const {
    assert(tabWidget_->indexOf(newSlnWidget_) == tabWidget_->count() - 1);
    return tabWidget_->count() - 1;
}

int BeautyEnhancementController::selectedSolutionIndexAfterRemove(int removedIndex) const {
    const int current = tabWidget_->currentIndex();
    if (isSolutionTab(current)) {
        return current;
    }

    const int remaining = solutionTabCount();
    if (remaining <= 0) {
        return -1;
    }

    return removedIndex < remaining ? removedIndex : remaining - 1;
}

void BeautyEnhancementController::refreshNewSolutionTabCloseButton() {
    const int index = tabWidget_->indexOf(newSlnWidget_);
    assert(index >= 0);
    if (index < 0) {
        return;
    }

    tabWidget_->tabBar()->setTabButton(index, QTabBar::LeftSide, nullptr);
    tabWidget_->tabBar()->setTabButton(index, QTabBar::RightSide, nullptr);
}

void BeautyEnhancementController::onTabCloseRequested(int index) {
    if (!isSolutionTab(index)) {
        assert(false);
        return;
    }
    if (solutionTabCount() <= 1) {
        assert(false);
        return;
    }

    auto* w = tabWidget_->widget(index);
    auto* solution = qobject_cast<SolutionController*>(w);
    assert(solution != nullptr);
    if (!loadingConfig_ && solution != nullptr) {
        reportBeautyEnhancementSolutionAction(app_,
                                              u8"solution_remove",
                                              solution->id(),
                                              to_u8(solution->displayName()));
    }
    int selected = -1;
    {
        QSignalBlocker blocker(tabWidget_);
        tabWidget_->removeTab(index);
        selected = selectedSolutionIndexAfterRemove(index);
        assert(selected >= 0);
        tabWidget_->setCurrentIndex(selected);
        refreshNewSolutionTabCloseButton();
    }
    
    delete w;
    if (!loadingConfig_) {
        auto patch = JsonUtility::Patch::remove(std::u8string(u8"/beauty/solutions/") + to_u8(index));
        JsonUtility::Patch::appendAdd(patch, u8"/beauty/selected", selected);
        emit settingPatchRequested(patch);
    }
}

void BeautyEnhancementController::onTabBarClicked(int index) {
    auto widget = tabWidget_->widget(index);
    if (widget != newSlnWidget_) {
        auto* solution = qobject_cast<SolutionController*>(widget);
        assert(solution != nullptr);
        if (solution != nullptr) {
            reportBeautyEnhancementSolutionAction(app_,
                                                  u8"solution_select",
                                                  solution->id(),
                                                  to_u8(solution->displayName()));
        }
        return;
    }

    auto solutionTab = new SolutionController(app_);
    solutionTab->setId(uniqueSolutionId<SolutionController>(tabWidget_));
    const auto solutionJson = beautySolutionSkeleton(app_, solutionTab->id());
    index = tabWidget_->count() - 1;
    assert(index >= 0);
    {
        QSignalBlocker blocker(tabWidget_);
        tabWidget_->insertTab(index, solutionTab, solutionTab->displayName());
        tabWidget_->setCurrentIndex(index);
        refreshNewSolutionTabCloseButton();
    }
    QObject::connect(solutionTab, &SolutionController::fieldChanged, this,
                     [this, solutionTab](const QString& key, const nlohmann::json& value) {
                         emitSolutionFieldPatch(solutionTab, key, value);
                     });

    auto patch = JsonUtility::Patch::add(u8"/beauty/solutions/-", solutionJson);
    JsonUtility::Patch::appendAdd(patch, u8"/beauty/selected", index);
    emit settingPatchRequested(patch);
    solutionTab->json2Me(solutionJson, static_cast<uint32_t>(index));
    reportBeautyEnhancementSolutionAction(app_,
                                          u8"solution_create",
                                          solutionTab->id(),
                                          to_u8(solutionTab->displayName()));
}

void BeautyEnhancementController::onCurrentChanged(int index) {
    if (!loadingConfig_ && index >= 0 && tabWidget_->widget(index) != newSlnWidget_) {
        emit settingPatchRequested(JsonUtility::Patch::add(u8"/beauty/selected", static_cast<uint32_t>(index)));
    }
}

void BeautyEnhancementController::onTabBarDoubleClicked(int index) {
    auto sln = qobject_cast<SolutionController*>(tabWidget_->widget(index));
    assert(sln != nullptr);

    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename Sln"), tr("Name:"),
                                               QLineEdit::Normal, sln->customName(), &ok);
    if (!ok) {
        return;
    }

    sln->setCustomName(name);
    tabWidget_->setTabText(index, sln->displayName());
    emit settingPatchRequested(JsonUtility::Patch::add(std::u8string(u8"/beauty/solutions/") + to_u8(index) + u8"/customName",
                                                 to_json(to_u8(name))));
    reportBeautyEnhancementSolutionAction(app_,
                                          u8"solution_rename",
                                          sln->id(),
                                          to_u8(sln->displayName()));
}

void BeautyEnhancementController::reset() {
    resetState(true);
}

void BeautyEnhancementController::resetState(bool) {
	assert(tabWidget_ != nullptr);
    QSignalBlocker tabBlocker(tabWidget_);
    for (int i = tabWidget_->count() - 1; i >= 0; --i) {
        QWidget* w = tabWidget_->widget(i);
        if (w == newSlnWidget_) {
            continue;
        }
        tabWidget_->removeTab(i);
        delete w;
    }

    QSignalBlocker blocker(enableBeautyCheckBox_);
    enableBeautyCheckBox_->setCheckState(Qt::Unchecked);
    if (newSlnWidget_ != nullptr && tabWidget_->indexOf(newSlnWidget_) >= 0) {
        tabWidget_->setCurrentWidget(newSlnWidget_);
    }
    refreshNewSolutionTabCloseButton();

    refreshEnableState(false);
}

void BeautyEnhancementController::json2Me(const nlohmann::json& j) {
    loadingConfig_ = true;
    resetState(false);

    bool enableBeauty = j.value("enableBeauty", false);
    uint32_t selected = j.value("selected", 0);
  
    {
        QSignalBlocker blocker(enableBeautyCheckBox_);
        enableBeautyCheckBox_->setCheckState(enableBeauty?Qt::Checked:Qt::Unchecked);
    }
    refreshEnableState(enableBeauty);
    const auto solutions = j.value("solutions", nlohmann::json::array());
    assert(solutions.is_array());
    for (auto i = 0; i < solutions.size(); i++) {
        const auto& s = solutions[i];
        SolutionController* sln = new SolutionController(app_);
        QObject::connect(sln, &SolutionController::fieldChanged, this,
                         [this, sln](const QString& key, const nlohmann::json& value) {
                             emitSolutionFieldPatch(sln, key, value);
                         });
        auto index = tabWidget_->count() - 1;
        assert(index >= 0);
        sln->json2Me(s, static_cast<uint32_t>(index));
        tabWidget_->insertTab(index, sln, sln->displayName());
    }
    refreshNewSolutionTabCloseButton();
    if (selected >= solutions.size()) {
        selected = static_cast<uint32_t>(solutions.size() - 1);
    }
    QSignalBlocker blocker(tabWidget_);
    tabWidget_->setCurrentIndex(static_cast<int>(selected));
    loadingConfig_ = false;
}

void BeautyEnhancementController::emitSolutionFieldPatch(SolutionController* solution,
                                                         const QString& key,
                                                         const nlohmann::json& value) {
    if (loadingConfig_) {
        return;
    }

    const int index = tabWidget_->indexOf(solution);
    if (index < 0 || tabWidget_->widget(index) == newSlnWidget_) {
        return;
    }

    emit settingPatchRequested(JsonUtility::Patch::add(std::u8string(u8"/beauty/solutions/")
                                                 + to_u8(index)
                                                 + u8"/"
                                                 + to_u8(key),
                                                 value));
}
