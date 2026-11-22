// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include <assert.h>
#include <iterator>
#include <vector>
#include <QString>
#include <QFont>
#include <QLabel>
#include <QSlider>
#include <QLineEdit>
#include <QCheckBox>
#include <QTabBar>
#include <QTabWidget>
#include <QInputDialog>
#include <QScrollArea>
#include <QScrollBar>
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
constexpr int kBeautyParamInset = 10;
constexpr int kBeautyParamSpacing = 14;
constexpr int kBeautySideSpacing = 5;
constexpr qreal kBeautySectionFontScale = 1.15;

void setBeautyTextStyle(QWidget* widget, QFont::Weight weight, qreal scale = 1.0) {
    auto font = widget->font();
    font.setWeight(weight);
    if (scale != 1.0) {
        if (font.pixelSize() > 0) {
            font.setPixelSize(qMax(1, qRound(font.pixelSize() * scale)));
        } else {
            font.setPointSizeF(font.pointSizeF() * scale);
        }
    }
    widget->setFont(font);
}

void styleBeautySideLabel(QLabel* label) {
    // OBS themes can set font-size for all widgets, overriding QWidget::setFont().
    label->setStyleSheet(QStringLiteral("font-size: 11.2pt; font-weight: normal;"));
}

QCheckBox* createBeautySectionCheckBox(const QString& name) {
    auto* checkBox = new QCheckBox(name);
    setBeautyTextStyle(checkBox, QFont::DemiBold, kBeautySectionFontScale);
    checkBox->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    return checkBox;
}

QScrollArea* scrollableBeautySection(QWidget* content) {
    auto* scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    content->layout()->setContentsMargins(kBeautyParamInset, 8, kBeautyParamInset, 8);
    content->layout()->setSpacing(kBeautyParamSpacing);
    scroll->setWidget(content);
    // QScrollArea fills with the palette's Base color by default.
    scroll->viewport()->setAutoFillBackground(false);
    content->setAutoFillBackground(false);
    return scroll;
}

void syncEffectDetailUi(const EffectDetailInfo& info) {
    QSignalBlocker sliderBlocker(info.slider_);
    info.slider_->setValue(static_cast<int>(info.value_));
    QSignalBlocker editorBlocker(info.editor_);
    info.editor_->setText(QString::number(info.value_));
}

std::u8string beautyParamPath(uint32_t solutionIndex, const char* key) {
    return std::u8string(u8"/beauty/solutions/") + to_u8(solutionIndex) + u8"/" + to_u8(key);
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
    skinBeauty_ = createBeautySectionCheckBox(tr("Skin Beauty"));
    
    skinParams_ = new QWidget;
    auto* skinLayout = new QVBoxLayout(skinParams_);
    skinLayout->setSpacing(kBeautyParamSpacing);
    addMainParamRow(skinLayout, tr("Whitening"), whiteness_);
    addMainParamRow(skinLayout, tr("Clarity"), clarity_);
    addMainParamRow(skinLayout, tr("Smoothing"), fullbodySmoothness_);

    faceShaping_ = createBeautySectionCheckBox(tr("Face Contouring"));
    
    tabBodyShaping_ = new QTabWidget;
    applyDisabledTabStyle(tabBodyShaping_);
    tabBodyShaping_->setStyleSheet(tabBodyShaping_->styleSheet() +
                                  QStringLiteral("QTabWidget::pane { border: none; }"));
    tabBodyShaping_->setContentsMargins(0, 0, 0, 0);
    auto* faceSection = scrollableBeautySection(createFaceTab());
    tabBodyShaping_->addTab(faceSection, tr("Face"));
    tabBodyShaping_->addTab(scrollableBeautySection(createEyeTab()), tr("Eye"));
    tabBodyShaping_->addTab(scrollableBeautySection(createNoseTab()), tr("Nose"));
    tabBodyShaping_->addTab(scrollableBeautySection(createMouthTab()), tr("Mouth"));
    tabBodyShaping_->addTab(scrollableBeautySection(createEyebrowTab()), tr("Eyebrow"));
    tabBodyShaping_->addTab(scrollableBeautySection(createOtherTab()), tr("Detail Retouching"));
    // Reserve the same scrollbar gutter above the tabs, even when a short tab is active.
    const int scrollBarWidth = faceSection->verticalScrollBar()->sizeHint().width();
    skinLayout->setContentsMargins(kBeautyParamInset, 0, kBeautyParamInset + scrollBarWidth, 0);
    
    QVBoxLayout* vboxTop = new QVBoxLayout;
    vboxTop->setContentsMargins(0, 0, 0, 0);
    vboxTop->addWidget(skinBeauty_);
    vboxTop->addWidget(skinParams_);
    vboxTop->addSpacing(4);
    vboxTop->addWidget(faceShaping_);
    vboxTop->addWidget(tabBodyShaping_, 1);
    
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
}

SolutionController::~SolutionController() {
    
}

void SolutionController::json2Me(const nlohmann::json& j) {
    json2Me(j, 0);
}

void SolutionController::json2Me(const nlohmann::json& j, uint32_t solutionIndex) {
    paramRequests_.reset();
    loadingConfig_ = true;
    setEnabled(false);
    std::vector<EffectDetailInfo*> params;
    std::vector<std::u8string> paths;
    auto readValue = [&params, &paths, solutionIndex](EffectDetailInfo& info, const char* key) {
        info.jsonKey_ = key;
        params.push_back(&info);
        paths.push_back(beautyParamPath(solutionIndex, key));
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

    auto* model = app_->filterModel();
    if (model == nullptr) {
        return;
    }
    model->queryEffectParamValues(
        paramRequests_, paths, [this, instanceId = model->filterUuid(), params, paths](const nlohmann::json& values) {
            const auto* currentModel = app_->filterModel();
            if (currentModel == nullptr || currentModel->filterUuid() != instanceId) {
                return;
            }
            for (size_t i = 0; i < paths.size(); ++i) {
                const auto value = values.find(to_char(paths[i]));
                if (value == values.end() || value->get<int>() < 0) {
                    LogWarn << "Beauty parameter unavailable, path=" << paths[i] << logEnd;
                    return;
                }
                params[i]->value_ = value->get<int>();
                syncEffectDetailUi(*params[i]);
            }
            setEnabled(true);
        });
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

QLabel* SolutionController::addParamRow(QVBoxLayout* layout, const QString& name, EffectDetailInfo& param) {
    auto* label = new QLabel(name);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    auto* slider = createSlider();
    auto* editor = createEdit();
    editor->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label->setBuddy(slider);
    slider->setAccessibleName(name);
    editor->setAccessibleName(name);
    bindSliderEditor(slider, editor, &param);

    auto* header = new QHBoxLayout;
    header->addWidget(label, 1);
    header->addWidget(editor, 0, Qt::AlignVCenter);
    auto* sliderRow = new QHBoxLayout;
    sliderRow->setContentsMargins(8, 0, 0, 0);
    sliderRow->addWidget(slider);

    auto* row = new QVBoxLayout;
    row->setSpacing(2);
    row->addLayout(header);
    row->addLayout(sliderRow);
    layout->addLayout(row);
    return label;
}

void SolutionController::addMainParamRow(QVBoxLayout* layout, const QString& name, EffectDetailInfo& param) {
    setBeautyTextStyle(addParamRow(layout, name, param), QFont::Medium);
}

void SolutionController::addParamGroup(QVBoxLayout* layout, const QString& name, EffectDetailInfo& param,
                                      const QString& leftName, EffectDetailInfo& left,
                                      const QString& rightName, EffectDetailInfo& right) {
    auto* groupLayout = new QVBoxLayout;
    groupLayout->setSpacing(kBeautySideSpacing);
    addMainParamRow(groupLayout, name, param);

    auto* sideLayout = new QVBoxLayout;
    sideLayout->setSpacing(kBeautySideSpacing);
    styleBeautySideLabel(addParamRow(sideLayout, leftName, left));
    styleBeautySideLabel(addParamRow(sideLayout, rightName, right));

    constexpr int guideOffset = 8;
    constexpr int guideWidth = 1;
    auto* guide = new QWidget;
    guide->setFixedWidth(guideWidth);
    guide->setStyleSheet(QStringLiteral("background-color: palette(button);"));

    auto* sideGroupLayout = new QHBoxLayout;
    sideGroupLayout->setContentsMargins(guideOffset, 0, 0, 0);
    sideGroupLayout->setSpacing(KHeadSpading - guideOffset - guideWidth);
    sideGroupLayout->addWidget(guide);
    sideGroupLayout->addLayout(sideLayout, 1);
    groupLayout->addLayout(sideGroupLayout);
    layout->addLayout(groupLayout);
}

QWidget* SolutionController::createFaceTab() {
    auto* tab = new QWidget;
    auto* layout = new QVBoxLayout(tab);
    addParamGroup(layout, tr("Slim face"), slimFace_,
                  tr("Slim Face Left"), slimFaceLeft_,
                  tr("Slim Face Right"), slimFaceRight_);
    addParamGroup(layout, tr("Small face"), smallFace_,
                  tr("Small Face Left"), smallFaceLeft_,
                  tr("Small Face Right"), smallFaceRight_);
    addMainParamRow(layout, tr("Face slimming"), faceSlimming_);
    addParamGroup(layout, tr("V Face"), vFace_,
                  tr("V Face Left"), vFaceLeft_,
                  tr("V Face Right"), vFaceRight_);
    addMainParamRow(layout, tr("Hairline"), hairLine_);
    addMainParamRow(layout, tr("Forehead Width"), foreheadWidth_);
    addMainParamRow(layout, tr("Forehead Height"), foreheadHeight_);
    addParamGroup(layout, tr("Temple"), temple_,
                  tr("Left Temple"), templeLeft_,
                  tr("Right Temple"), templeRight_);
    addParamGroup(layout, tr("Zygomatic Bone"), zygomaticBone_,
                  tr("Zygomatic Bone Left"), zygomaticBoneLeft_,
                  tr("Zygomatic Bone Right"), zygomaticBoneRight_);
    addParamGroup(layout, tr("Mandible"), mandible_,
                  tr("Mandible Left"), mandibleLeft_,
                  tr("Mandible Right"), mandibleRight_);
    addMainParamRow(layout, tr("Chin"), chin_);
    addParamGroup(layout, tr("Pointed Chin"), pointedChin_,
                  tr("Pointed Chin Left"), pointedChinLeft_,
                  tr("Pointed Chin Right"), pointedChinRight_);
    layout->addStretch();
    return tab;
}

QWidget* SolutionController::createEyeTab() {
    auto* tab = new QWidget;
    auto* layout = new QVBoxLayout(tab);
    addParamGroup(layout, tr("Big eye"), bigEye_,
                  tr("Big eye Left"), bigEyeLeft_,
                  tr("Big eye Right"), bigEyeRight_);
    addParamGroup(layout, tr("Eye height"), eyeHeight_,
                  tr("Eye height Left"), eyeHeightLeft_,
                  tr("Eye height Right"), eyeHeightRight_);
    addParamGroup(layout, tr("Eye width"), eyeWidth_,
                  tr("Eye width Left"), eyeWidthLeft_,
                  tr("Eye width Right"), eyeWidthRight_);
    addParamGroup(layout, tr("Eye distance"), eyeDistance_,
                  tr("Eye distance Left"), eyeDistanceLeft_,
                  tr("Eye distance Right"), eyeDistanceRight_);
    addParamGroup(layout, tr("Eye corner"), eyeCorner_,
                  tr("Eye corner Left"), eyeCornerLeft_,
                  tr("Eye corner Right"), eyeCornerRight_);
    addParamGroup(layout, tr("Eye pos"), eyePos_,
                  tr("Eye pos Left"), eyePosLeft_,
                  tr("Eye pos Right"), eyePosRight_);
    layout->addStretch();
    return tab;
}

QWidget* SolutionController::createNoseTab() {
    auto* tab = new QWidget;
    auto* layout = new QVBoxLayout(tab);
    addMainParamRow(layout, tr("Slim nose"), slimNose_);
    addParamGroup(layout, tr("Slim nostril"), slimNostril_,
                  tr("Slim nostril Left"), slimNostrilLeft_,
                  tr("Slim nostril Right"), slimNostrilRight_);
    addMainParamRow(layout, tr("Slim nose bridge"), slimNoseBridge_);
    addMainParamRow(layout, tr("Straighten nose"), straightenNose_);
    addMainParamRow(layout, tr("Nose pos"), nosePos_);
    layout->addStretch();
    return tab;
}

QWidget* SolutionController::createMouthTab() {
    auto* tab = new QWidget;
    auto* layout = new QVBoxLayout(tab);
    addMainParamRow(layout, tr("Mouth"), mouth_);
    addMainParamRow(layout, tr("Mouth tilt"), mouthTilt_);
    addMainParamRow(layout, tr("Philtrum"), philtrum_);
    addMainParamRow(layout, tr("Mouth pos"), mouthPos_);
    layout->addStretch();
    return tab;
}

QWidget* SolutionController::createEyebrowTab() {
    auto* tab = new QWidget;
    auto* layout = new QVBoxLayout(tab);
    addParamGroup(layout, tr("Eyebrow thickness"), eyebrowThickness_,
                  tr("Eyebrow thickness Left"), eyebrowThicknessLeft_,
                  tr("Eyebrow thickness Right"), eyebrowThicknessRight_);
    addParamGroup(layout, tr("Eyebrow distance"), eyebrowDistance_,
                  tr("Eyebrow distance Left"), eyebrowDistanceLeft_,
                  tr("Eyebrow distance Right"), eyebrowDistanceRight_);
    addParamGroup(layout, tr("Eyebrow tilt"), eyebrowTilt_,
                  tr("Eyebrow tilt Left"), eyebrowTiltLeft_,
                  tr("Eyebrow tilt Right"), eyebrowTiltRight_);
    addParamGroup(layout, tr("Eyebrow pos"), eyebrowPos_,
                  tr("Eyebrow pos Left"), eyebrowPosLeft_,
                  tr("Eyebrow pos Right"), eyebrowPosRight_);
    layout->addStretch();
    return tab;
}

QWidget* SolutionController::createOtherTab() {
    auto* tab = new QWidget;
    auto* layout = new QVBoxLayout(tab);
    addMainParamRow(layout, tr("Forehead line"), foreheadLine_);
    addMainParamRow(layout, tr("Smile line"), smileLine_);
    addMainParamRow(layout, tr("Whiten teeth"), whitenTeeth_);
    addMainParamRow(layout, tr("Dark circle"), darkCircle_);
    addMainParamRow(layout, tr("Brighten eye"), brightenEye_);
    addMainParamRow(layout, tr("Mid face"), midFace_);
    addMainParamRow(layout, tr("Lower face"), lowerFace_);
    layout->addStretch();
    return tab;
}

void SolutionController::refreshEnableSkinState() {
    skinParams_->setEnabled(enableSkin_);
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
    
    enableBeautyCheckBox_ = createCheckBox(tr("Enable beauty"));

    QHBoxLayout* hbox = new QHBoxLayout;
    hbox->addWidget(camera);
    hbox->addWidget(cameraName);
    hbox->addStretch();
    hbox->addWidget(enableBeautyCheckBox_);
    
    tabWidget_ = new QTabWidget;
    applyDisabledTabStyle(tabWidget_);
    QVBoxLayout* vbox = new QVBoxLayout;
    vbox->addItem(hbox);
    vbox->addWidget(tabWidget_, 1);
    setLayout(vbox);
    
    newSlnWidget_ = new QWidget;
    tabWidget_->addTab(newSlnWidget_, tr("+ New Sln"));
    refreshSolutionTabCloseButtons();
    
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

void BeautyEnhancementController::refreshSolutionTabCloseButtons() {
    const int index = tabWidget_->indexOf(newSlnWidget_);
    assert(index >= 0);
    if (index < 0) {
        return;
    }

    tabWidget_->setTabsClosable(solutionTabCount() > 1);
    auto* tabBar = tabWidget_->tabBar();
    for (const auto side : {QTabBar::LeftSide, QTabBar::RightSide}) {
        if (auto* button = tabBar->tabButton(index, side)) {
            tabBar->setTabButton(index, side, nullptr);
            button->deleteLater();
        }
    }
}

void BeautyEnhancementController::onTabCloseRequested(int index) {
    if (!isSolutionTab(index)) {
        assert(false);
        return;
    }
    if (solutionTabCount() <= 1) {
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
        refreshSolutionTabCloseButtons();
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
        refreshSolutionTabCloseButtons();
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
    refreshSolutionTabCloseButtons();

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
    refreshSolutionTabCloseButtons();
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
