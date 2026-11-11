// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include <algorithm>

#include <QLabel>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QRadioButton>
#include <QPushButton>
#include <QCheckBox>
#include <QSlider>
#include <QLineEdit>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QTabWidget>
#include <QStackedWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPointer>
#include <QSignalBlocker>
#include <QStringList>

#include "nlohmann/json.hpp"
#include "commonui.h"
#include "filtermodelhelpers.h"
#include "assetsmgr.h"
#include "imagetextwidget.h"
#include "keyingcolorpickwidget.h"
#include "nokeyingzonewidget.h"
#include "keyingsettingcontroller.h"
#include "../modelapp.h"
#include "misc.h"
#include "mediatype.h"
#include "../qstringconv.h"
#include "jsonutility.h"
#include "../statistics/settingspageanalytics.h"

namespace {
constexpr int kMaskTypePolygon = 0;
constexpr int kMaskTypeCircle = 1;
constexpr uint32_t kDefaultKeyingColor = (0u << 24) | (255u << 16) | (0u << 8) | 255u;

QIcon noKeyingZoneShapeIcon(bool circle) {
    constexpr int kIconSize = 20;
    QPixmap pixmap(kIconSize, kIconSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(180, 185, 195), 2.0));
    if (circle) {
        painter.drawEllipse(QRectF(3.0, 3.0, 14.0, 14.0));
    } else {
        painter.drawRect(QRectF(2.0, 5.0, 16.0, 10.0));
    }
    return QIcon(pixmap);
}

QString backgroundAssetPatterns(std::span<const std::u8string_view> extensions) {
    QStringList patterns;
    for (const auto extension : extensions) {
        patterns.push_back(QStringLiteral("*") + from_u8(std::u8string(extension)));
    }
    return patterns.join(QLatin1Char(' '));
}

double clampNormalized(double v) {
    return std::clamp(v, 0.0, 1.0);
}

}

void EffectDetailInfo2::bind(QSlider* slider, QLineEdit* editor) {
    slider_ = slider;
    editor_ = editor;
}

void EffectDetailInfo2::setJsonKey(const char* key) {
    jsonKey_ = key;
}

void EffectDetailInfo2::setValue(uint32_t value) {
	assert(slider_ != nullptr);
	assert(editor_ != nullptr);
    int uiValue = static_cast<int>(value);
    uiValue = std::clamp(uiValue, slider_->minimum(), slider_->maximum());

    value_ = static_cast<uint32_t>(uiValue);
}

void EffectDetailInfo2::syncValueToUI() {
	assert(slider_ != nullptr);
	assert(editor_ != nullptr);
    QSignalBlocker blocker(slider_);
    slider_->setValue(value_);

    QSignalBlocker blocker2(editor_);
    editor_->setText(QString::number(value_));
}

void EffectDetailInfo2::json2Me(const nlohmann::json &j, uint32_t defaultValue) {
	assert(jsonKey_ != nullptr);
    if (!j.is_object()) {
        setValue(defaultValue);
        return;
    }

    auto it = j.find(jsonKey_);
    if (it == j.end()) {
        setValue(defaultValue);
        return;
    }
    if (it->is_number_unsigned()) {
        setValue(it->get<uint32_t>());
        return;
    }
    if (it->is_number_integer()) {
        const auto value = it->get<int64_t>();
        assert(value >= 0);
        setValue(value >= 0 ? static_cast<uint32_t>(value) : 0);
        return;
    }

    assert(false);
    setValue(defaultValue);
}

void EffectDetailInfo2::me2Json(nlohmann::json& j) const {
	assert(jsonKey_ != nullptr);
    j[jsonKey_] = value_;
}

KeyingTemplateController::KeyingTemplateController(GlobalModel *app,
                                                   const char* templateName,
                                                   QWidget *parent /* = nullptr*/)
    : CiciWidget(parent), app_(app), templateName_(templateName) {
    assert(templateName_ != nullptr);
    initializeParams();

    QVBoxLayout* vbox = new QVBoxLayout;
    {
        QPushButton* btn = createPushButton(tr("Reset Param"));
        QHBoxLayout* hbox = new QHBoxLayout;
        hbox->addWidget(btn);
        hbox->addStretch();
        vbox->addItem(hbox);
        
        QObject::connect(btn, &QPushButton::clicked, this, [this]() {
            reportKeyingTemplateAction(app_, u8"template_reset", to_u8(templateName_));
            onResetParamBtnClicked();
        });
    }
    //basic
    {
        QLabel* label = createFixWidthLabel(tr("Similarity"));
        auto slider = createSlider();
        auto editor = createEdit();
        QHBoxLayout* hbox = new QHBoxLayout;
        hbox->setSpacing(KSpacing);
        hbox->addWidget(label);
        hbox->addWidget(slider);
        hbox->addWidget(editor);
        vbox->addItem(hbox);
        
        bindSliderEditor(slider, editor, &similarity_);
    }
    {
        QLabel* label = createFixWidthMarqueeLabel(tr("Background blur"));
        auto slider = createSlider();
        auto editor = createEdit();
        QHBoxLayout* hbox = new QHBoxLayout;
        hbox->setSpacing(KSpacing);
        hbox->addWidget(label);
        hbox->addWidget(slider);
        hbox->addWidget(editor);
        vbox->addItem(hbox);
        
        bindSliderEditor(slider, editor, &bkBlur_);
    }
    //Transparency
    {
        QLabel* label = createFixWidthMarqueeLabel(tr("Transparency Coarse"));
        QSlider* slider = createSlider();
        QLineEdit* editor = createEdit();
        QHBoxLayout* hbox = new QHBoxLayout;
        hbox->setSpacing(KSpacing);
        hbox->addWidget(label);
        hbox->addWidget(slider);
        hbox->addWidget(editor);
        vbox->addItem(hbox);
        
        bindSliderEditor(slider, editor, &transparencyCorse_);
    }
    {
        QLabel* label = createFixWidthMarqueeLabel(tr("Transparency Fine"));
        QSlider* slider = createSlider();
        QLineEdit* editor = createEdit();
        QHBoxLayout* hbox = new QHBoxLayout;
        hbox->setSpacing(KSpacing);
        hbox->addWidget(label);
        hbox->addWidget(slider);
        hbox->addWidget(editor);
        vbox->addItem(hbox);
        
        bindSliderEditor(slider, editor, &transparencyFine_);
    }
    {
        QLabel* label = createFixWidthMarqueeLabel(tr("Edge Indent"));
        QSlider* slider = createSlider();
        QLineEdit* editor = createEdit();
        QHBoxLayout* hbox = new QHBoxLayout;
        hbox->setSpacing(KSpacing);
        hbox->addWidget(label);
        hbox->addWidget(slider);
        hbox->addWidget(editor);
        vbox->addItem(hbox);
        
        bindSliderEditor(slider, editor, &edgeIndent_);
    }
    //blend smoothing
    {
        QLabel* label = createLabel(tr("Blend Smoothly"));
        blendSmoothCheckBox_ = createCheckBox("");
        QHBoxLayout* hbox = new QHBoxLayout;
        hbox->setSpacing(KSpacing);
        hbox->addWidget(label);
        hbox->addWidget(blendSmoothCheckBox_);
        hbox->addStretch();
        vbox->addItem(hbox);
        
        auto b = QObject::connect(blendSmoothCheckBox_, &QCheckBox::stateChanged, this, &KeyingTemplateController::onBlendSmoothStateChanged);
        assert(b);
        QObject::connect(blendSmoothCheckBox_, &QCheckBox::clicked, this, [this](bool checked) {
            reportKeyingTemplateToggle(app_, to_u8(templateName_), u8"blend_smooth", checked);
        });
        {
            QLabel* label = createFixWidthLabel(tr("Degree"));
            QSlider* slider = createSlider();
            QLineEdit* editor = createEdit();
            blendSmoothUI_.push_back(label);
            blendSmoothUI_.push_back(slider);
            blendSmoothUI_.push_back(editor);
            QHBoxLayout* hbox = new QHBoxLayout;
            hbox->setSpacing(KSpacing);
            hbox->addWidget(label);
            hbox->addWidget(slider);
            hbox->addWidget(editor);
            vbox->addItem(hbox);
            
            bindSliderEditor(slider, editor, &blendSmoothDegree_);
        }
        {
            QLabel* label = createFixWidthLabel(tr("Scope"));
            QSlider* slider = createSlider();
            QLineEdit* editor = createEdit();
            blendSmoothUI_.push_back(label);
            blendSmoothUI_.push_back(slider);
            blendSmoothUI_.push_back(editor);
            QHBoxLayout* hbox = new QHBoxLayout;
            hbox->setSpacing(KSpacing);
            hbox->addWidget(label);
            hbox->addWidget(slider);
            hbox->addWidget(editor);
            vbox->addItem(hbox);
            
            bindSliderEditor(slider, editor, &blendSmoothScope_);
        }
    }
    //Light & Shadow
    {
        QLabel* label = createLabel(tr("Light&Shadow"));
        lightShadowCheckBox_ = createCheckBox("");
        QHBoxLayout* hbox = new QHBoxLayout;
        hbox->setSpacing(KSpacing);
        hbox->addWidget(label);
        hbox->addWidget(lightShadowCheckBox_);
        hbox->addStretch();
        vbox->addItem(hbox);
        
        auto b = QObject::connect(lightShadowCheckBox_, &QCheckBox::stateChanged, this, &KeyingTemplateController::onLightShadowStateChanged);
        assert(b);
        QObject::connect(lightShadowCheckBox_, &QCheckBox::clicked, this, [this](bool checked) {
            reportKeyingTemplateToggle(app_, to_u8(templateName_), u8"light_shadow", checked);
        });
        {
            QLabel* label = createFixWidthLabel(tr("Degree"));
            QSlider* slider = createSlider();
            QLineEdit* editor = createEdit();
            lightShadowUI_.push_back(label);
            lightShadowUI_.push_back(slider);
            lightShadowUI_.push_back(editor);
            QHBoxLayout* hbox = new QHBoxLayout;
            hbox->setSpacing(KSpacing);
            hbox->addWidget(label);
            hbox->addWidget(slider);
            hbox->addWidget(editor);
            vbox->addItem(hbox);
            
            bindSliderEditor(slider, editor, &lightShadowDegree_);
        }
        {
            QLabel* label = createFixWidthLabel(tr("Scope"));
            QSlider* slider = createSlider();
            QLineEdit* editor = createEdit();
            lightShadowUI_.push_back(label);
            lightShadowUI_.push_back(slider);
            lightShadowUI_.push_back(editor);
            QHBoxLayout* hbox = new QHBoxLayout;
            hbox->setSpacing(KSpacing);
            hbox->addWidget(label);
            hbox->addWidget(slider);
            hbox->addWidget(editor);
            vbox->addItem(hbox);
            
            bindSliderEditor(slider, editor, &lightShadowScope_);
        }
    }
    //hair detail
    {
        QLabel* label = createLabel(tr("Hair detail"));
        hairDetailCheckBox_ = createCheckBox("");
        QHBoxLayout* hbox = new QHBoxLayout;
        hbox->setSpacing(KSpacing);
        hbox->addWidget(label);
        hbox->addWidget(hairDetailCheckBox_);
        hbox->addStretch();
        vbox->addItem(hbox);
            
        auto b = QObject::connect(hairDetailCheckBox_, &QCheckBox::stateChanged, this, &KeyingTemplateController::onHairDetailStateChanged);
        assert(b);
        QObject::connect(hairDetailCheckBox_, &QCheckBox::clicked, this, [this](bool checked) {
            reportKeyingTemplateToggle(app_, to_u8(templateName_), u8"hair_detail", checked);
        });
        {
            QLabel* label = createFixWidthLabel(tr("Scope"));
            QSlider* slider = createSlider();
            QLineEdit* editor = createEdit();
            hairDetailUI_.push_back(label);
            hairDetailUI_.push_back(slider);
            hairDetailUI_.push_back(editor);
            QHBoxLayout* hbox = new QHBoxLayout;
            hbox->setSpacing(KSpacing);
            hbox->addWidget(label);
            hbox->addWidget(slider);
            hbox->addWidget(editor);
            vbox->addItem(hbox);
            
            bindSliderEditor(slider, editor, &hairDetailScope_);
        }
    }
    //Color bleed control
    {
        QLabel* label = createLabel(tr("Color Bleed"));
        colorBleedCheckBox_ = createCheckBox("");
        QHBoxLayout* hbox = new QHBoxLayout;
        hbox->setSpacing(KSpacing);
        hbox->addWidget(label);
        hbox->addWidget(colorBleedCheckBox_);
        hbox->addStretch();
        vbox->addItem(hbox);
        
        auto b = QObject::connect(colorBleedCheckBox_, &QCheckBox::stateChanged, this, &KeyingTemplateController::onColorBleedControlStateChanged);
        assert(b);
        QObject::connect(colorBleedCheckBox_, &QCheckBox::clicked, this, [this](bool checked) {
            reportKeyingTemplateToggle(app_, to_u8(templateName_), u8"color_bleed", checked);
        });
        {
            QLabel* label = createFixWidthLabel(tr("Red"));
            QSlider* slider = createSlider();
            QLineEdit* editor = createEdit();
            colorBleedUI_.push_back(label);
            colorBleedUI_.push_back(slider);
            colorBleedUI_.push_back(editor);
            QHBoxLayout* hbox = new QHBoxLayout;
            hbox->setSpacing(KSpacing);
            hbox->addWidget(label);
            hbox->addWidget(slider);
            hbox->addWidget(editor);
            vbox->addItem(hbox);
            
            bindSliderEditor(slider, editor, &colorBleedRed_);
        }
        {
            QLabel* label = createFixWidthLabel(tr("Green"));
            QSlider* slider = createSlider();
            QLineEdit* editor = createEdit();
            colorBleedUI_.push_back(label);
            colorBleedUI_.push_back(slider);
            colorBleedUI_.push_back(editor);
            QHBoxLayout* hbox = new QHBoxLayout;
            hbox->setSpacing(KSpacing);
            hbox->addWidget(label);
            hbox->addWidget(slider);
            hbox->addWidget(editor);
            vbox->addItem(hbox);
            
            bindSliderEditor(slider, editor, &colorBleedGreen_);
        }
        {
            QLabel* label = createFixWidthLabel(tr("Blue"));
            QSlider* slider = createSlider();
            QLineEdit* editor = createEdit();
            colorBleedUI_.push_back(label);
            colorBleedUI_.push_back(slider);
            colorBleedUI_.push_back(editor);
            QHBoxLayout* hbox = new QHBoxLayout;
            hbox->setSpacing(KSpacing);
            hbox->addWidget(label);
            hbox->addWidget(slider);
            hbox->addWidget(editor);
            vbox->addItem(hbox);
            
            bindSliderEditor(slider, editor, &colorBleedBlue_);
        }
        {
            QLabel* label = createFixWidthMarqueeLabel(tr("Mix Coefficient"));
            QSlider* slider = createSlider();
            QLineEdit* editor = createEdit();
            colorBleedUI_.push_back(label);
            colorBleedUI_.push_back(slider);
            colorBleedUI_.push_back(editor);
            QHBoxLayout* hbox = new QHBoxLayout;
            hbox->setSpacing(KSpacing);
            hbox->addWidget(label);
            hbox->addWidget(slider);
            hbox->addWidget(editor);
            vbox->addItem(hbox);
            
            bindSliderEditor(slider, editor, &colorBleedMixCoefficient_);
        }
        {
            QLabel* label = createFixWidthLabel(tr("Intensity"));
            QSlider* slider = createSlider();
            QLineEdit* editor = createEdit();
            colorBleedUI_.push_back(label);
            colorBleedUI_.push_back(slider);
            colorBleedUI_.push_back(editor);
            QHBoxLayout* hbox = new QHBoxLayout;
            hbox->setSpacing(KSpacing);
            hbox->addWidget(label);
            hbox->addWidget(slider);
            hbox->addWidget(editor);
            vbox->addItem(hbox);
            
            bindSliderEditor(slider, editor, &colorBleedIntensity_);
        }
        {
            QLabel* label = createFixWidthLabel(tr("Luminance"));
            QSlider* slider = createSlider();
            QLineEdit* editor = createEdit();
            colorBleedUI_.push_back(label);
            colorBleedUI_.push_back(slider);
            colorBleedUI_.push_back(editor);
            QHBoxLayout* hbox = new QHBoxLayout;
            hbox->setSpacing(KSpacing);
            hbox->addWidget(label);
            hbox->addWidget(slider);
            hbox->addWidget(editor);
            vbox->addItem(hbox);
            
            bindSliderEditor(slider, editor, &colorBleedLuminance_);
        }
    }
    //face protect
    {
        QLabel* label = createLabel(tr("Face Protect"));
        faceProtectCheckBox_ = createCheckBox("");
        QHBoxLayout* hbox = new QHBoxLayout;
        hbox->setSpacing(KSpacing);
        hbox->addWidget(label);
        hbox->addWidget(faceProtectCheckBox_);
        hbox->addStretch();
        vbox->addItem(hbox);
        
        auto b = QObject::connect(faceProtectCheckBox_, &QCheckBox::stateChanged, this, &KeyingTemplateController::onFaceProtectStateChanged);
        assert(b);
        QObject::connect(faceProtectCheckBox_, &QCheckBox::clicked, this, [this](bool checked) {
            reportKeyingTemplateToggle(app_, to_u8(templateName_), u8"face_protect", checked);
        });
    }
    
    this->setLayout(vbox);
    syncFeatureFlagUI();
}

void KeyingTemplateController::initializeParams() {
    similarity_.setJsonKey("similarity");

    bkBlur_.setJsonKey("backgroundBlur");
    
    transparencyCorse_.setJsonKey("transparencyCoarse");

    transparencyFine_.setJsonKey("transparencyFine");

    edgeIndent_.setJsonKey("edgeIndent");
    
    //LightWrapFlag
    blendSmoothDegree_.setJsonKey("blendSmoothDegree");

    blendSmoothScope_.setJsonKey("blendSmoothScope");
    
    //colorAdjFlag
    lightShadowDegree_.setJsonKey("lightShadowDegree");

    lightShadowScope_.setJsonKey("lightShadowScope");
    
    //EdgeFlag
    hairDetailScope_.setJsonKey("hairDetailScope");
    
    //SpillSupEnable & SpillSupMethod
    colorBleedRed_.setJsonKey("colorBleedRed");

    colorBleedGreen_.setJsonKey("colorBleedGreen");

    colorBleedBlue_.setJsonKey("colorBleedBlue");

    colorBleedMixCoefficient_.setJsonKey("colorBleedMixCoefficient");

    colorBleedIntensity_.setJsonKey("colorBleedIntensity");

    colorBleedLuminance_.setJsonKey("colorBleedLuminance");
    
    params_ = {
        &similarity_,
        &bkBlur_,
        &transparencyCorse_,
        &transparencyFine_,
        &edgeIndent_,
        &blendSmoothDegree_,
        &blendSmoothScope_,
        &lightShadowDegree_,
        &lightShadowScope_,
        &hairDetailScope_,
        &colorBleedRed_,
        &colorBleedGreen_,
        &colorBleedBlue_,
        &colorBleedMixCoefficient_,
        &colorBleedIntensity_,
        &colorBleedLuminance_,
    };
}

void KeyingTemplateController::active() {
    active_ = true;
    syncFeatureFlagUI();
}

void KeyingTemplateController::deactive() {
    active_ = false;
}

KeyingTemplateController::~KeyingTemplateController() {
    
}

std::u8string KeyingTemplateController::paramPath(const EffectDetailInfo2& param) const {
    assert(templateName_ != nullptr);
    assert(param.jsonKey_ != nullptr);
    return std::u8string(u8"/keying/greenKeying/") + to_u8(templateName_) + u8"/" + to_u8(param.jsonKey_);
}

uint32_t KeyingTemplateController::resolvedParamValue(const EffectDetailInfo2& param,
                                                      bool defaultOnly) const {
    assert(app_ != nullptr);
    auto* model = app_->filterModel();
    assert(model != nullptr);

    int value = 0;
    const auto path = paramPath(param);
    const bool resolved = defaultOnly
        ? model->defaultEffectParamValue(path.c_str(), value)
        : model->effectParamValue(path.c_str(), value);
    assert(resolved);
    assert(value >= 0);
    return resolved && value >= 0 ? static_cast<uint32_t>(value) : 0;
}

void KeyingTemplateController::reset() {
    loadingConfig_ = true;
    for (auto* param : params_) {
        param->setValue(resolvedParamValue(*param, true));
        param->syncValueToUI();
    }

    resetFeatureFlags();
    loadingConfig_ = false;
}

void KeyingTemplateController::json2Me(const nlohmann::json& j) {
    loadingConfig_ = true;

    const auto empty = nlohmann::json::object();
    const auto& source = j.is_object() ? j : empty;

    for (auto *param : params_) {
        param->json2Me(source, resolvedParamValue(*param, false));
	    param->syncValueToUI();
    }

    blendSmoothEnabled_ = source.value("blendSmoothEnabled", false);
    lightShadowEnabled_ = source.value("lightShadowEnabled", false);
    hairDetailEnabled_ = source.value("hairDetailEnabled", false);
    colorBleedEnabled_ = source.value("colorBleedEnabled", false);
    faceProtectEnabled_ = source.value("faceProtectEnabled", false);
    syncFeatureFlagUI();
    loadingConfig_ = false;
}

nlohmann::json KeyingTemplateController::me2Json() const {
    auto j = nlohmann::json::object();
    for (const auto* param: params_) {
        param->me2Json(j);
    }

    j["blendSmoothEnabled"] = blendSmoothEnabled_;
    j["lightShadowEnabled"] = lightShadowEnabled_;
    j["hairDetailEnabled"] = hairDetailEnabled_;
    j["colorBleedEnabled"] = colorBleedEnabled_;
    j["faceProtectEnabled"] = faceProtectEnabled_;
    
    return j;
}

void KeyingTemplateController::bindSliderEditor(QSlider* slider, QLineEdit* editor, EffectDetailInfo2* pData) {
    assert(pData != nullptr);
    pData->bind(slider, editor);
    
    QObject::connect(slider, &QSlider::valueChanged, this, [this, pData](int value) {
	    pData->setValue(value);
        pData->syncValueToUI();
        if (!loadingConfig_) {
            emit valueFieldChanged(QString::fromUtf8(pData->jsonKey_), pData->value_);
        }
    });
    QObject::connect(editor, &QLineEdit::editingFinished, this, [this, editor, pData]() {
	    const bool userEdited = editor->isModified();
        editor->setModified(false);
	    const uint32_t oldValue = pData->value_;
	    pData->setValue(editor->text().toUInt());
	    pData->syncValueToUI();
        if (!loadingConfig_) {
            emit valueFieldChanged(QString::fromUtf8(pData->jsonKey_), pData->value_);
            if (userEdited && oldValue != pData->value_) {
                reportKeyingTemplateParamInput(app_,
                                               to_u8(templateName_),
                                               to_u8(pData->jsonKey_),
                                               static_cast<int>(pData->value_));
            }
        }
    });
}

void KeyingTemplateController::onResetParamBtnClicked() {
    for (auto* param: params_) {
        param->setValue(resolvedParamValue(*param, true));
	    param->syncValueToUI();
        if (!loadingConfig_) {
            emit valueFieldChanged(QString::fromUtf8(param->jsonKey_), param->value_);
        }
    }

    resetFeatureFlags();
    if (!loadingConfig_) {
        emit toggleFieldChanged(QStringLiteral("blendSmoothEnabled"), false);
        emit toggleFieldChanged(QStringLiteral("lightShadowEnabled"), false);
        emit toggleFieldChanged(QStringLiteral("hairDetailEnabled"), false);
        emit toggleFieldChanged(QStringLiteral("colorBleedEnabled"), false);
        emit toggleFieldChanged(QStringLiteral("faceProtectEnabled"), false);
    }
}

void KeyingTemplateController::syncFeatureFlagUI() {
    auto setChecked = [](QCheckBox* checkBox, bool checked) {
        QSignalBlocker blocker(checkBox);
        checkBox->setChecked(checked);
    };
	auto setEnabled = [](const QList<QWidget *> &items, bool visible) {
        for (auto* item: items) {
            item->setEnabled(visible);
        }
    };

    setChecked(blendSmoothCheckBox_, blendSmoothEnabled_);
    setChecked(lightShadowCheckBox_, lightShadowEnabled_);
    setChecked(hairDetailCheckBox_, hairDetailEnabled_);
    setChecked(colorBleedCheckBox_, colorBleedEnabled_);
    setChecked(faceProtectCheckBox_, faceProtectEnabled_);

    setEnabled(blendSmoothUI_, blendSmoothEnabled_);
    setEnabled(lightShadowUI_, lightShadowEnabled_);
    setEnabled(hairDetailUI_, hairDetailEnabled_);
    setEnabled(colorBleedUI_, colorBleedEnabled_);
}

void KeyingTemplateController::applyFeatureFlags() {
}

void KeyingTemplateController::resetFeatureFlags() {
    blendSmoothEnabled_ = false;
    lightShadowEnabled_ = false;
    hairDetailEnabled_ = false;
    colorBleedEnabled_ = false;
    faceProtectEnabled_ = false;
    syncFeatureFlagUI();
    applyFeatureFlags();
}

void KeyingTemplateController::onBlendSmoothStateChanged(int s) {
    blendSmoothEnabled_ = s == Qt::Checked;
    syncFeatureFlagUI();
    if (!loadingConfig_) {
        emit toggleFieldChanged(QStringLiteral("blendSmoothEnabled"), blendSmoothEnabled_);
    }
}

void KeyingTemplateController::onLightShadowStateChanged(int s) {
    lightShadowEnabled_ = s == Qt::Checked;
    syncFeatureFlagUI();
    if (!loadingConfig_) {
        emit toggleFieldChanged(QStringLiteral("lightShadowEnabled"), lightShadowEnabled_);
    }
}

void KeyingTemplateController::onHairDetailStateChanged(int s) {
    hairDetailEnabled_ = s == Qt::Checked;
    syncFeatureFlagUI();
    if (!loadingConfig_) {
        emit toggleFieldChanged(QStringLiteral("hairDetailEnabled"), hairDetailEnabled_);
    }
}

void KeyingTemplateController::onColorBleedControlStateChanged(int s) {
    colorBleedEnabled_ = s == Qt::Checked;
    syncFeatureFlagUI();
    if (!loadingConfig_) {
        emit toggleFieldChanged(QStringLiteral("colorBleedEnabled"), colorBleedEnabled_);
    }
}

void KeyingTemplateController::onFaceProtectStateChanged(int s) {
    faceProtectEnabled_ = s == Qt::Checked;
    syncFeatureFlagUI();
    if (!loadingConfig_) {
        emit toggleFieldChanged(QStringLiteral("faceProtectEnabled"), faceProtectEnabled_);
    }
}

void GreenScreenKeyingController::NoKeyingZone::json2Me(const nlohmann::json& j) {
    tag = from_json(j["tag"]);
    assert(!tag.isEmpty());
    x = clampNormalized(j.value("x", 0.0));
    y = clampNormalized(j.value("y", 0.0));
    width = clampNormalized(j.value("width", 0.0));
    height = clampNormalized(j.value("height", 0.0));
    const uint32_t maskType = j.value("type", static_cast<uint32_t>(kMaskTypePolygon));
    type = maskType == kMaskTypeCircle ? kMaskTypeCircle : kMaskTypePolygon;
}

nlohmann::json GreenScreenKeyingController::NoKeyingZone::me2Json() const {
    auto j = nlohmann::json::object();
    j["tag"] = to_json(tag);
    j["x"] = x;
    j["y"] = y;
    j["width"] = width;
    j["height"] = height;
    j["type"] = type;
    
    return j;
}

GreenScreenKeyingController::GreenScreenKeyingController(GlobalModel *app, QWidget *parent /* = nullptr*/)
	: CiciWidget(parent), app_(app) {
}

void GreenScreenKeyingController::createUI() {
    constexpr int kKeyingParamLabelWidth = 110;
    constexpr int kColorPickValueLabelWidth = 90;
    QVBoxLayout* vbox = new QVBoxLayout;
    {
        QLabel* label = createLabel(tr("Keying param"));
        QHBoxLayout* hbox = new QHBoxLayout;
        hbox->addWidget(label);
        hbox->addStretch();
        vbox->addItem(hbox);
    }
    {
        QLabel* label = createFixWidthMarqueeLabel(tr("Color picking"), kKeyingParamLabelWidth);
        colorPickLabel_ = createFixWidthLabel("", kColorPickValueLabelWidth);
        QPushButton* aiPick = createMarqueeButton(tr("AI color pick"));
        QPushButton* manualPick = createMarqueeButton(tr("manual color pick"));
        QHBoxLayout* hbox = new QHBoxLayout;
        hbox->setSpacing(KSpacing);
        hbox->addWidget(label);
        hbox->addWidget(colorPickLabel_);
        hbox->addWidget(aiPick);
        hbox->addWidget(manualPick);
        hbox->addStretch();
        vbox->addItem(hbox);
        
        QObject::connect(manualPick, &QPushButton::clicked, this, &GreenScreenKeyingController::onManualColorPickingBtnClicked);
        QObject::connect(aiPick, &QPushButton::clicked, this, &GreenScreenKeyingController::onAIColorPickingBtnClicked);
    }
    {
        QLabel* label = createFixWidthMarqueeLabel(tr("No-keying zone"), kKeyingParamLabelWidth);
        QPushButton* btn = createMarqueeButton(tr("Set nokeying zone"));
        QHBoxLayout* hbox = new QHBoxLayout;
        hbox->setSpacing(KSpacing);
        hbox->addWidget(label);
        hbox->addStretch();
        hbox->addWidget(btn);
        vbox->addItem(hbox);
        
        QObject::connect(btn, &QPushButton::clicked, this, &GreenScreenKeyingController::onSetNoKeyingZoneBtnClicked);
    }
    {
        QLabel* label = createLabel(tr("Param template"));
        QHBoxLayout* hbox = new QHBoxLayout;
        hbox->addWidget(label);
        hbox->addStretch();
        vbox->addItem(hbox);
    }
    {
        templateTabWidget_ = new QTabWidget;
        applyDisabledTabStyle(templateTabWidget_);
        template1_ = new KeyingTemplateController(app_, "template1");
        template2_ = new KeyingTemplateController(app_, "template2");
        QObject::connect(template1_, &KeyingTemplateController::valueFieldChanged, this,
                         [this](const QString& key, const nlohmann::json& value) {
                             emit templateValueChanged(QStringLiteral("template1/") + key, value);
                         });
        QObject::connect(template1_, &KeyingTemplateController::toggleFieldChanged, this,
                         [this](const QString& key, const nlohmann::json& value) {
                             emit templateToggleChanged(QStringLiteral("template1/") + key, value);
                         });
        QObject::connect(template2_, &KeyingTemplateController::valueFieldChanged, this,
                         [this](const QString& key, const nlohmann::json& value) {
                             emit templateValueChanged(QStringLiteral("template2/") + key, value);
                         });
        QObject::connect(template2_, &KeyingTemplateController::toggleFieldChanged, this,
                         [this](const QString& key, const nlohmann::json& value) {
                             emit templateToggleChanged(QStringLiteral("template2/") + key, value);
                         });
        templateTabWidget_->addTab(template1_ , tr("Template 1"));
        templateTabWidget_->addTab(template2_ , tr("Template 2"));
        vbox->addWidget(templateTabWidget_);
        auto b = QObject::connect(templateTabWidget_, &QTabWidget::currentChanged, this, &GreenScreenKeyingController::onCurrentChanged);
        assert(b);
        QObject::connect(templateTabWidget_, &QTabWidget::tabBarClicked, this, [this](int index) {
            if (index < 0 || index > 1) {
                return;
            }
            reportKeyingTemplateAction(app_,
                                       u8"template_select",
                                       index == 0 ? u8"template1" : u8"template2");
        });
    }
    {
        QLabel* label = createLabel(tr("Background assets"));
        QPushButton* btn = createMarqueeButton(tr("Load local assets"));
        clearBkAssetBtn_ = createPushButton(tr("Clear"));
        clearBkAssetBtn_->setEnabled(false);
        QHBoxLayout* hbox = new QHBoxLayout;
        hbox->setSpacing(KSpacing);
        hbox->addWidget(label);
        hbox->addItem(new QSpacerItem(20, 20, QSizePolicy::Expanding, QSizePolicy::Fixed));
        hbox->addWidget(btn);
        hbox->addWidget(clearBkAssetBtn_);
        vbox->addItem(hbox);
        
        QObject::connect(btn, &QPushButton::clicked, this, &GreenScreenKeyingController::onLoadLocalAssetBtnClicked);
        QObject::connect(clearBkAssetBtn_, &QPushButton::clicked, this, &GreenScreenKeyingController::onClearBackgroundAssetBtnClicked);
    }
    {
        assetNameLabel_ = createMarqueeLabel(tr("No background asset"));
        QHBoxLayout* hbox = new QHBoxLayout;
        hbox->addWidget(assetNameLabel_);
        hbox->addStretch();
        vbox->addItem(hbox);
    }
    {
        bkAsset_ = new ImageTextWidget(app_->assets());
        bkAsset_->setFixedSize(72, 72);
        bkAsset_->setHoverEffectEnabled(false);
        bkAsset_->setFavoriteEnabled(false);
        bkAsset_->setClickableEnabled(false);
        bkAsset_->setVisible(false);
        QHBoxLayout* hbox = new QHBoxLayout;
        hbox->addWidget(bkAsset_);
        hbox->addStretch();
        vbox->addItem(hbox);
    }
    
    vbox->addStretch();
    this->setLayout(vbox);
}

void GreenScreenKeyingController::json2Me(const nlohmann::json& j) {
    loadingConfig_ = true;
    resetState(false);
    loadingConfig_ = true;

    const auto empty = nlohmann::json::object();
    const auto& source = j.is_object() ? j : empty;

    uint32_t selectedColor = source.value("selectedColor", colorPicked_);
    setColorLabel(selectedColor);
    bkFile_ = from_json(source.value("bkFile", nlohmann::json()));
    updateBackgroundAssetUI();
    nokeyingZoneEnabled_ = source.value("nokeyingZoneEnabled", false);
    const auto zones = source.value("zones", nlohmann::json::array());
    zones_.clear();
    for (auto i = 0; i < zones.size(); i++) {
        NoKeyingZone z;
        z.json2Me(zones[i]);
        zones_.push_back(z);
    }

    syncNokeyingZoneWidgetFromState();
    applyNokeyingZoneUiState();
    
    template1_->json2Me(source.value("template1", nlohmann::json::object()));
    template2_->json2Me(source.value("template2", nlohmann::json::object()));
    const uint32_t selectedTemplate = source.value("selectedTemplate", 0);
    if (selectedTemplate < static_cast<uint32_t>(templateTabWidget_->count())) {
        QSignalBlocker blocker(templateTabWidget_);
        templateTabWidget_->setCurrentIndex(static_cast<int>(selectedTemplate));
    }
    onCurrentChanged(templateTabWidget_->currentIndex());
    loadingConfig_ = false;
}

void GreenScreenKeyingController::reset() {
    resetState(true);
}

void GreenScreenKeyingController::resetState(bool resetTemplates) {
    loadingConfig_ = true;

    zones_.clear();

    nokeyingZoneEnabled_ = false;
    syncNokeyingZoneWidgetFromState();
    applyNokeyingZoneUiState();

    bkFile_.clear();
    updateBackgroundAssetUI();

    setColorLabel(kDefaultKeyingColor);

    QSignalBlocker blocker(templateTabWidget_);
    templateTabWidget_->setCurrentIndex(0);
    if (resetTemplates) {
        template1_->reset();
        template2_->reset();
    }

    effectActive_ = false;
    template1_->deactive();
    template2_->deactive();
    loadingConfig_ = false;
}

nlohmann::json GreenScreenKeyingController::me2Json() const {
    auto j = nlohmann::json::object();
    j["selectedColor"] = colorPicked_;
    j["bkFile"] = to_json(bkFile_);
    j["nokeyingZoneEnabled"] = nokeyingZoneEnabled_;
    auto arr = nlohmann::json::array();
    for (auto i = 0; i < zones_.size(); i++) {
        arr.push_back(zones_[i].me2Json());
    }
    j["zones"].swap(arr);
    j["selectedTemplate"] = templateTabWidget_->currentIndex();
    j["template1"] = template1_->me2Json();
    j["template2"] = template2_->me2Json();
    
    return j;
}

GreenScreenKeyingController::~GreenScreenKeyingController() {
    delete keyingColorWindow_;
    delete nokeyingZoneWindow_;
}

QWidget* GreenScreenKeyingController::createColorPickWidget() {
    QWidget* frame = new QWidget;
    frame->setWindowTitle(tr("Pick keying color"));
    applyCiciWindowIcon(frame);
    QVBoxLayout* vbox = new QVBoxLayout;
    QLabel* label = createLabel(tr("Pick color close to the figure edge ; make keying more accurate."));
    KeyingColorPickWidget* k = new KeyingColorPickWidget(app_);
    vbox->addWidget(label);
    vbox->addWidget(k, 0, Qt::AlignHCenter);
    frame->setLayout(vbox);
    QObject::connect(k, &KeyingColorPickWidget::colorPicked, this, &GreenScreenKeyingController::onColorPicked);
    
    return frame;
}

QWidget* GreenScreenKeyingController::createNokeyingZoneWidget() {
    QWidget* frame = new QWidget;
    frame->setWindowTitle(tr("Setting nokeying zone"));
    applyCiciWindowIcon(frame);
    QVBoxLayout* vbox = new QVBoxLayout;
    {
        QHBoxLayout* hbox = new QHBoxLayout;
        nokeyingZoneCheckBox_ = createCheckBox(tr("Enable nokeying zone"));
        resetBtn_ = createMarqueeButton(tr("Reset nokeying zone"));
        hbox->addWidget(nokeyingZoneCheckBox_);
        hbox->addItem(createExpandingHorizontalSpacer());
        hbox->addWidget(resetBtn_);
        vbox->addItem(hbox);
        
        QObject::connect(nokeyingZoneCheckBox_, &QCheckBox::stateChanged, this, &GreenScreenKeyingController::onEnableNokeyingStateChanged);
        QObject::connect(nokeyingZoneCheckBox_, &QCheckBox::clicked, this, [this](bool checked) {
            reportKeyingToggle(app_, u8"no_keying_zone_toggle", checked);
        });
        QObject::connect(resetBtn_, &QPushButton::clicked, this, &GreenScreenKeyingController::onResetBtnClicked);
    }
    {
        QHBoxLayout* hbox = new QHBoxLayout;
        hbox->setSpacing(KSpacing);
        {
            QVBoxLayout* vbox = new QVBoxLayout;
            rectangleBtn_ = createPushButton("");
            rectangleBtn_->setCheckable(true);
            rectangleBtn_->setChecked(true);
            rectangleBtn_->setIcon(noKeyingZoneShapeIcon(false));
            rectangleBtn_->setIconSize(QSize(20, 20));
            rectangleBtn_->setFixedSize(36, 36);
            rectangleBtn_->setToolTip(tr("Rectangle"));
            circleBtn_ = createPushButton("");
            circleBtn_->setCheckable(true);
            circleBtn_->setIcon(noKeyingZoneShapeIcon(true));
            circleBtn_->setIconSize(QSize(20, 20));
            circleBtn_->setFixedSize(36, 36);
            circleBtn_->setToolTip(tr("Circle"));
            vbox->addWidget(rectangleBtn_);
            vbox->addWidget(circleBtn_);
            vbox->addStretch();
            hbox->addItem(vbox);
            QObject::connect(rectangleBtn_, &QPushButton::clicked, this, &GreenScreenKeyingController::onRectangleBtnClicked);
            QObject::connect(circleBtn_, &QPushButton::clicked, this, &GreenScreenKeyingController::onCircleBtnClicked);
        }
        
        nokeyingZoneWidget_ = new NokeyingZoneWidget(app_);
        syncNokeyingZoneWidgetFromState();
        hbox->addWidget(nokeyingZoneWidget_);
        
        QObject::connect(nokeyingZoneWidget_, &NokeyingZoneWidget::zoneCreated, this, &GreenScreenKeyingController::onZoneCreated);
        QObject::connect(nokeyingZoneWidget_, &NokeyingZoneWidget::zoneDeleted, this, &GreenScreenKeyingController::onZoneDeleted);
        vbox->addItem(hbox);
    }
    applyNokeyingZoneUiState();
    
    vbox->addStretch();
    frame->setLayout(vbox);
    
    return frame;
}

void GreenScreenKeyingController::updateBackgroundAssetUI() {
    bkAsset_->setVisible(false);

    if (bkFile_.isEmpty()) {
        clearBkAssetBtn_->setEnabled(false);
        assetNameLabel_->setText(tr("No background asset"));
        assetNameLabel_->setToolTip(QString());
        return;
    }

    clearBkAssetBtn_->setEnabled(true);

    QFileInfo info(bkFile_);
    if (!info.exists() || !info.isFile()) {
        assetNameLabel_->setText(tr("Missing background asset"));
        assetNameLabel_->setToolTip(bkFile_);
        return;
    }

    assetNameLabel_->setText(info.fileName());
    assetNameLabel_->setToolTip(bkFile_);

    if (BackgroundAsset::isImage(to_u8(bkFile_))) {
        bkAsset_->setImage(bkFile_);
        bkAsset_->setVisible(true);
    }
}

void GreenScreenKeyingController::active() {
    effectActive_ = true;
    onCurrentChanged(templateTabWidget_->currentIndex());
}

void GreenScreenKeyingController::deactive() {
    effectActive_ = false;
    template1_->deactive();
    template2_->deactive();
}

void GreenScreenKeyingController::onCurrentChanged(int index) {
    if (index == 0) {
        template2_->deactive();
        template1_->active();
    }
    else if (index == 1) {
        template1_->deactive();
        template2_->active();
    }
    emitSelectedTemplateChanged();
}

void GreenScreenKeyingController::onAIColorPickingBtnClicked() {
    reportKeyingAction(app_, u8"ai_color_pick_click");
    QPointer<GreenScreenKeyingController> self(this);
    GPURenderWidget::autoKeying(app_, this, [self](uint32_t rgba, float similarity) {
        (void)similarity;
        if (self == nullptr) {
            return;
        }
        self->setColorLabel(rgba);
        self->emitSelectedColorChanged();
    });
}

void GreenScreenKeyingController::onManualColorPickingBtnClicked() {
    reportKeyingAction(app_, u8"manual_color_pick_click");
    if (keyingColorWindow_ == nullptr) {
        keyingColorWindow_ = createColorPickWidget();
    }

    keyingColorWindow_->show();
    keyingColorWindow_->raise();
    keyingColorWindow_->activateWindow();
}

void GreenScreenKeyingController::onSetNoKeyingZoneBtnClicked() {
    reportKeyingAction(app_, u8"no_keying_zone_editor_open");
    if (nokeyingZoneWindow_ == nullptr) {
        nokeyingZoneWindow_ = createNokeyingZoneWidget();
    }

    syncNokeyingZoneWidgetFromState();
    applyNokeyingZoneUiState();
    nokeyingZoneWindow_->show();
    nokeyingZoneWindow_->raise();
    nokeyingZoneWindow_->activateWindow();
}

void GreenScreenKeyingController::onLoadLocalAssetBtnClicked() {
    reportKeyingAction(app_, u8"background_asset_browse_click");
    const QString initialDir = bkFile_.isEmpty() ? QString("~/") : QFileInfo(bkFile_).absolutePath();
    const QString imagePatterns = backgroundAssetPatterns(BackgroundAsset::imageExtensions());
    const QString videoPatterns = backgroundAssetPatterns(BackgroundAsset::videoExtensions());
    const QString assetPatterns = imagePatterns + QLatin1Char(' ') + videoPatterns;
    const QString fileName = QFileDialog::getOpenFileName(
        this,
        tr("Open Background Asset"),
        initialDir,
        tr("Background Assets (%1);;Images (%2);;Videos (%3)")
            .arg(assetPatterns, imagePatterns, videoPatterns)
    );
    if (fileName.isEmpty()) {
        return;
    }

    QFileInfo info(fileName);
    if (!info.exists() || !info.isFile()) {
        QMessageBox::warning(this, tr("Open Background Asset"), tr("The selected background asset does not exist."));
        return;
    }

    if (!BackgroundAsset::isSupported(to_u8(fileName))) {
        QMessageBox::warning(this, tr("Open Background Asset"), tr("The selected background asset type is not supported."));
        return;
    }

    bkFile_ = info.absoluteFilePath();
    updateBackgroundAssetUI();
    emitBackgroundAssetChanged();
    reportKeyingBackgroundAssetSelect(
        app_, BackgroundAsset::isImage(to_u8(bkFile_)) ? u8"image" : u8"video");
}

void GreenScreenKeyingController::onClearBackgroundAssetBtnClicked() {
    if (bkFile_.isEmpty()) {
        return;
    }

    bkFile_.clear();
    updateBackgroundAssetUI();
    emitBackgroundAssetChanged();
    reportKeyingAction(app_, u8"background_asset_clear");
}

void GreenScreenKeyingController::onColorPicked(unsigned int v) {
    setColorLabel(v);
    keyingColorWindow_->hide();
    emitSelectedColorChanged();
    reportKeyingAction(app_, u8"manual_color_select");
}

void GreenScreenKeyingController::setColorLabel(uint32_t v) {
    colorPicked_ = v;
    uint8_t r = static_cast<uint8_t>(v >> 24);
    uint8_t g = static_cast<uint8_t>(v >> 16);
    uint8_t b = static_cast<uint8_t>(v >> 8);
    uint8_t a = static_cast<uint8_t>(v);
    QString txt = QString("#%1%2%3%4")
        .arg(r, 2, 16, QChar('0')) // width:2, Hex fill:'0'
        .arg(g, 2, 16, QChar('0'))
	    .arg(b, 2, 16, QChar('0'))
        .arg(a, 2, 16, QChar('0'));
    colorPickLabel_->setText(txt);
}

nlohmann::json GreenScreenKeyingController::maskShapeJson(const NoKeyingZone& zone) const {
    auto shape = nlohmann::json::object();
    shape["tag"] = to_json(zone.tag);
    const bool isCircle = zone.type == kMaskTypeCircle;
    shape["type"] = isCircle ? kMaskTypeCircle : kMaskTypePolygon;

    auto points = nlohmann::json::array();
    const double left = clampNormalized(zone.x);
    const double top = clampNormalized(zone.y);
    const double right = clampNormalized(zone.x + zone.width);
    const double bottom = clampNormalized(zone.y + zone.height);

    if (isCircle) {
        const double centerX = clampNormalized(zone.x + zone.width / 2.0);
        const double centerY = clampNormalized(zone.y + zone.height / 2.0);
        const double radius = clampNormalized(zone.width / 2.0);
        points.push_back(centerX);
        points.push_back(centerY);
        points.push_back(radius);
    } else {
        points.push_back(left);
        points.push_back(top);
        points.push_back(right);
        points.push_back(top);
        points.push_back(right);
        points.push_back(bottom);
        points.push_back(left);
        points.push_back(bottom);
    }

    shape["points"].swap(points);
    return shape;
}

void GreenScreenKeyingController::applyNokeyingZoneUiState() {
    if (nokeyingZoneWidget_ == nullptr) {
        return;
    }

    if (nokeyingZoneCheckBox_->isChecked() != nokeyingZoneEnabled_) {
        QSignalBlocker blocker(nokeyingZoneCheckBox_);
        nokeyingZoneCheckBox_->setChecked(nokeyingZoneEnabled_);
    }

    resetBtn_->setEnabled(nokeyingZoneEnabled_);
    rectangleBtn_->setEnabled(nokeyingZoneEnabled_);
    circleBtn_->setEnabled(nokeyingZoneEnabled_);
    nokeyingZoneWidget_->setEnabled(nokeyingZoneEnabled_);
}

void GreenScreenKeyingController::syncNokeyingZoneWidgetFromState() {
    if (nokeyingZoneWidget_ == nullptr) {
        return;
    }

    QList<NokeyingZoneWidget::ZoneData> widgetZones;
    for (const auto& zone : zones_) {
        NokeyingZoneWidget::ZoneData widgetZone;
        widgetZone.tag = zone.tag;
        widgetZone.type = zone.type == kMaskTypeCircle ? NokeyingZoneWidget::Circle : NokeyingZoneWidget::Rectangle;
        widgetZone.x = zone.x;
        widgetZone.y = zone.y;
        widgetZone.width = zone.width;
        widgetZone.height = zone.height;
        widgetZones.append(widgetZone);
    }
    nokeyingZoneWidget_->setZones(widgetZones);

    rectangleBtn_->setChecked(true);
    circleBtn_->setChecked(false);
    nokeyingZoneWidget_->setZoneType(NokeyingZoneWidget::Rectangle);
}

void GreenScreenKeyingController::onEnableNokeyingStateChanged(int s) {
    nokeyingZoneEnabled_ = s == Qt::Checked;
    applyNokeyingZoneUiState();
    emitNokeyingZoneEnabledChanged();
}

void GreenScreenKeyingController::onResetBtnClicked() {
    reportKeyingAction(app_, u8"no_keying_zone_reset");
    zones_.clear();
    syncNokeyingZoneWidgetFromState();
    emitZonesChanged();
}

void GreenScreenKeyingController::onRectangleBtnClicked() {
    reportKeyingZoneShapeSelect(app_, u8"rectangle");
    rectangleBtn_->setChecked(true);
    circleBtn_->setChecked(false);
    nokeyingZoneWidget_->setZoneType(NokeyingZoneWidget::Rectangle);
}

void GreenScreenKeyingController::onCircleBtnClicked() {
    reportKeyingZoneShapeSelect(app_, u8"circle");
    circleBtn_->setChecked(true);
    rectangleBtn_->setChecked(false);
    nokeyingZoneWidget_->setZoneType(NokeyingZoneWidget::Circle);
}

void GreenScreenKeyingController::onZoneCreated(const QString& tag, int type, double x, double y, double width, double height) {
    reportKeyingAction(app_, u8"no_keying_zone_create");
    NoKeyingZone zone;
    assert(!tag.isEmpty());
    zone.tag = tag;
    zone.type = type == kMaskTypeCircle ? kMaskTypeCircle : kMaskTypePolygon;
    zone.x = clampNormalized(x);
    zone.y = clampNormalized(y);
    zone.width = clampNormalized(width);
    zone.height = clampNormalized(height);
    zones_.push_back(zone);
    emitZonesChanged();
}

void GreenScreenKeyingController::onZoneDeleted(const QString& tag) {
    assert(!tag.isEmpty());
    auto it = std::find_if(zones_.begin(), zones_.end(), [&tag](const NoKeyingZone& zone) {
        return zone.tag == tag;
    });
    if (it == zones_.end()) {
        return;
    }

    zones_.erase(it);
    emitZonesChanged();
    reportKeyingAction(app_, u8"no_keying_zone_delete");
}

nlohmann::json GreenScreenKeyingController::zonesJson() const {
    auto zones = nlohmann::json::array();
    for (const auto& zone : zones_) {
        zones.push_back(zone.me2Json());
    }

    return zones;
}

void GreenScreenKeyingController::emitBasicStateFieldChanged(const QString& path,
                                                             const nlohmann::json& value) {
    if (loadingConfig_) {
        return;
    }

    emit basicStateChanged(path, value);
}

void GreenScreenKeyingController::emitNokeyingAssetFieldChanged(const QString& path,
                                                                const nlohmann::json& value) {
    if (loadingConfig_) {
        return;
    }

    emit nokeyingAssetChanged(path, value);
}

void GreenScreenKeyingController::emitSelectedColorChanged() {
    emitBasicStateFieldChanged(QStringLiteral("selectedColor"), colorPicked_);
}

void GreenScreenKeyingController::emitSelectedTemplateChanged() {
    emitBasicStateFieldChanged(QStringLiteral("selectedTemplate"), templateTabWidget_->currentIndex());
}

void GreenScreenKeyingController::emitBackgroundAssetChanged() {
    emitNokeyingAssetFieldChanged(QStringLiteral("bkFile"), to_json(bkFile_));
}

void GreenScreenKeyingController::emitNokeyingZoneEnabledChanged() {
    emitNokeyingAssetFieldChanged(QStringLiteral("nokeyingZoneEnabled"), nokeyingZoneEnabled_);
}

void GreenScreenKeyingController::emitZonesChanged() {
    emitNokeyingAssetFieldChanged(QStringLiteral("zones"), zonesJson());
}

KeyingSettingController::KeyingSettingController(GlobalModel *app, QWidget *parent /* = nullptr*/)
	: CiciWidget(parent), app_(app) {
    assert(app_ != nullptr);
}

void KeyingSettingController::createUI() {
    QVBoxLayout* vbox = new QVBoxLayout;
    {
        QLabel* cameraLabel = createLabel(tr("Camera:"));
        QLabel* cameraNameLabel = createVideoInputLabel(app_, tr("No Camera"));
        QHBoxLayout* hbox = new QHBoxLayout;
        hbox->addWidget(cameraLabel);
        hbox->addWidget(cameraNameLabel);
        hbox->addStretch();
        vbox->addItem(hbox);
    }
    
    {
        noKeying_ = createRadioButton(tr("No Keying"));
        gsKeying_ = createRadioButton(tr("Green screen keying"));
        nsSegment_ = createRadioButton(tr("Natural scene segment (Coming soon)"));
        nsSegment_->setEnabled(false);
        nsSegment_->setStyleSheet(QStringLiteral(
            "QRadioButton:disabled { color: #F3D38A; }"));
        nsSegment_->setToolTip(tr("Natural scene keying is coming soon."));
        QHBoxLayout* hbox = new QHBoxLayout;
        hbox->setSpacing(KSpacing);
        hbox->addWidget(noKeying_);
        hbox->addWidget(gsKeying_);
        hbox->addWidget(nsSegment_);
        hbox->addStretch();
        vbox->addItem(hbox);
        
        QObject::connect(noKeying_, &QRadioButton::toggled, this, [this](bool checked) {
            if (checked) {
                gsKeyingTab_->deactive();
                stackedWidget_->setCurrentIndex(0);
                emitKeyingModePatch(0);
            }
        });
        QObject::connect(gsKeying_, &QRadioButton::toggled, this, [this](bool checked) {
            if (checked) {
                gsKeyingTab_->active();
                stackedWidget_->setCurrentIndex(1);
                emitKeyingModePatch(1);
            }
        });
        QObject::connect(nsSegment_, &QRadioButton::toggled, this, [this](bool checked) {
            if (checked) {
                gsKeyingTab_->deactive();
                stackedWidget_->setCurrentIndex(2);
                emitKeyingModePatch(2);
            }
        });
        QObject::connect(noKeying_, &QRadioButton::clicked, this, [this]() {
            reportKeyingModeSelect(app_, u8"none");
        });
        QObject::connect(gsKeying_, &QRadioButton::clicked, this, [this]() {
            reportKeyingModeSelect(app_, u8"green_screen");
        });
        QObject::connect(nsSegment_, &QRadioButton::clicked, this, [this]() {
            reportKeyingModeSelect(app_, u8"natural_scene");
        });
    }
    
    QWidget* noKeyingTab = new QWidget;
    gsKeyingTab_ = new GreenScreenKeyingController(app_);
    gsKeyingTab_->createUI();
    QObject::connect(gsKeyingTab_, &GreenScreenKeyingController::basicStateChanged, this,
                     &KeyingSettingController::emitGreenKeyingBasicPatch);
    QObject::connect(gsKeyingTab_, &GreenScreenKeyingController::templateValueChanged, this,
                     &KeyingSettingController::emitGreenKeyingTemplateValuePatch);
    QObject::connect(gsKeyingTab_, &GreenScreenKeyingController::templateToggleChanged, this,
                     &KeyingSettingController::emitGreenKeyingTemplateTogglePatch);
    QObject::connect(gsKeyingTab_, &GreenScreenKeyingController::nokeyingAssetChanged, this,
                     &KeyingSettingController::emitGreenKeyingNokeyingAssetPatch);
    QWidget* nsSegTab = new QWidget;
    {
        QVBoxLayout* vbox = new QVBoxLayout;
        {
            QLabel* bkLabel = createLabel(tr("Backgroud assets"));
            QPushButton* btn = createMarqueeButton(tr("Add local file"));
            QHBoxLayout* hbox = new QHBoxLayout;
            hbox->addWidget(bkLabel);
            hbox->addItem(new QSpacerItem(20, 20, QSizePolicy::Expanding, QSizePolicy::Fixed));
            hbox->addWidget(btn);
            vbox->addItem(hbox);
            QObject::connect(btn, &QPushButton::clicked, this, &KeyingSettingController::onAddLocalBtnClicked);
        }
        
        {
            bkAssets_ = createLabel("");
            QString path = from_path(FileSystemOp::resourcesPath() / u8"circle@1x.png");
            QPixmap pix = app_->assets()->image(path);
            bkAssets_->setFixedSize(pix.size());
            bkAssets_->setPixmap(pix);
            QHBoxLayout* hbox = new QHBoxLayout;
            hbox->addWidget(bkAssets_);
            hbox->addStretch();
            vbox->addItem(hbox);
        }
        
        {
            assetNameLabel_ = createMarqueeLabel(tr("Asset name"));
            QHBoxLayout* hbox = new QHBoxLayout;
            hbox->addWidget(assetNameLabel_);
            hbox->addStretch();
            vbox->addItem(hbox);
        }
        
        vbox->addStretch();
        
        nsSegTab->setLayout(vbox);
    }
    
    stackedWidget_ = new QStackedWidget;
    stackedWidget_->addWidget(noKeyingTab);
    stackedWidget_->addWidget(gsKeyingTab_);
    stackedWidget_->addWidget(nsSegTab);
    
    vbox->addWidget(stackedWidget_, 100);
    
    addScrollBar(vbox);
}

KeyingSettingController::~KeyingSettingController() {
}

void KeyingSettingController::switchLanguage() {
}

void KeyingSettingController::onAddLocalBtnClicked() {
    QString fileName = QFileDialog::getOpenFileName(this, tr("Open Assets File"),
                                                    "~/",
                                                    tr("Image Files(*.png *.jpg *.bmp *.xpm);;Video (*.mp4);;All (*)"));
    if (fileName.isEmpty()) {
        return ;
    }
    
    //todo ...
    assert(false);
}

void KeyingSettingController::json2Me(const nlohmann::json& j) {
    loadingConfig_ = true;
    resetState(false);
    loadingConfig_ = true;

    const auto empty = nlohmann::json::object();
    const auto& source = j.is_object() ? j : empty;

    uint32_t mode = source.value("mode", 0);
    if (mode == 2) {
        mode = 0;
    }

    assetNameLabel_->setText(from_json(source.value("bkFile", nlohmann::json())));
    gsKeyingTab_->json2Me(source.value("greenKeying", nlohmann::json::object()));

    if (mode == 0) {
        QSignalBlocker blocker(noKeying_);
        QSignalBlocker blocker1(gsKeying_);
        QSignalBlocker blocker2(nsSegment_);
        noKeying_->setChecked(true);
        gsKeying_->setChecked(false);
        nsSegment_->setChecked(false);
        stackedWidget_->setCurrentIndex(0);
    }
    else if (mode == 1) {
        QSignalBlocker blocker(noKeying_);
        QSignalBlocker blocker1(gsKeying_);
        QSignalBlocker blocker2(nsSegment_);
        noKeying_->setChecked(false);
        gsKeying_->setChecked(true);
        nsSegment_->setChecked(false);
        stackedWidget_->setCurrentIndex(1);
    }
    else if (mode == 2) {
        QSignalBlocker blocker(noKeying_);
        QSignalBlocker blocker1(gsKeying_);
        QSignalBlocker blocker2(nsSegment_);
        noKeying_->setChecked(false);
        gsKeying_->setChecked(false);
        nsSegment_->setChecked(true);
        stackedWidget_->setCurrentIndex(2);
    }
    loadingConfig_ = false;
}

void KeyingSettingController::reset() {
    resetState(true);
}

void KeyingSettingController::resetState(bool resetGreenKeying) {
    loadingConfig_ = true;
    if (resetGreenKeying) {
        gsKeyingTab_->reset();
    }
    else {
        gsKeyingTab_->deactive();
    }

    QSignalBlocker blocker(noKeying_);
    noKeying_->setChecked(true);
    QSignalBlocker blocker1(gsKeying_);
    gsKeying_->setChecked(false);
    QSignalBlocker blocker2(nsSegment_);
    nsSegment_->setChecked(false);
    QSignalBlocker blocker3(stackedWidget_);
    stackedWidget_->setCurrentIndex(0);
    assetNameLabel_->setText(QString());
    loadingConfig_ = false;
}

void KeyingSettingController::emitKeyingModePatch(uint32_t mode) {
    if (loadingConfig_) {
        return;
    }

    emit settingPatchRequested(JsonUtility::Patch::add(u8"/keying/mode", mode));
}

void KeyingSettingController::emitGreenKeyingPatch(const QString& path,
                                                   const nlohmann::json& value) {
    if (loadingConfig_) {
        return;
    }

    emit settingPatchRequested(JsonUtility::Patch::add(std::u8string(u8"/keying/greenKeying/") + to_u8(path), value));
}

void KeyingSettingController::emitGreenKeyingBasicPatch(const QString& path,
                                                        const nlohmann::json& value) {
    emitGreenKeyingPatch(path, value);
}

void KeyingSettingController::emitGreenKeyingTemplateValuePatch(const QString& path,
                                                               const nlohmann::json& value) {
    emitGreenKeyingPatch(path, value);
}

void KeyingSettingController::emitGreenKeyingTemplateTogglePatch(const QString& path,
                                                                const nlohmann::json& value) {
    emitGreenKeyingPatch(path, value);
}

void KeyingSettingController::emitGreenKeyingNokeyingAssetPatch(const QString& path,
                                                               const nlohmann::json& value) {
    emitGreenKeyingPatch(path, value);
}
