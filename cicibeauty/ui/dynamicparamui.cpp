// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include <assert.h>
#include <QFont>
#include <QLabel>
#include <QSlider>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include "dynamicparamui.h"
#include "commonui.h"
#include "../qstringconv.h"

void DynamicParamUIItem::create() {
    label_ = new QLabel;
    label_->setTextFormat(Qt::PlainText);
    label_->setWordWrap(true);
    auto font = label_->font();
    font.setWeight(QFont::Medium);
    label_->setFont(font);

    slider_ = createSlider();
    editor_ = createEdit();
    editor_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label_->setBuddy(slider_);

    auto* header = new QHBoxLayout;
    header->addWidget(label_, 1);
    header->addWidget(editor_, 0, Qt::AlignVCenter);

    auto* sliderRow = new QHBoxLayout;
    sliderRow->setContentsMargins(8, 0, 0, 0);
    sliderRow->addWidget(slider_);

    layout_ = new QVBoxLayout;
    layout_->setContentsMargins(10, 0, 10, 0);
    layout_->setSpacing(2);
    layout_->addLayout(header);
    layout_->addLayout(sliderRow);
}

void DynamicParamUIItem::setIntValue(uint32_t v) {
    slider_->setValue(v);
    editor_->setText(QString("%1").arg(v));
}

void DynamicParamUIItem::setFloatValue(float f) {
	auto v = valueToUI(f);
	setIntValue(v);
}

void DynamicParamUIItem::setEnabled(bool enabled) {
    label_->setEnabled(enabled);
    slider_->setEnabled(enabled);
    editor_->setEnabled(enabled);
}

void DynamicParamUIItem::show() {
    label_->show();
    slider_->show();
    editor_->show();
}

void DynamicParamUIItem::hide() {
    label_->hide();
    slider_->hide();
    editor_->hide();
}

void DynamicParamUIItem::setParam(const std::u8string& uiName, const std::u8string& paramName, float def, float max, float min, uint32_t filterIndex) {
    paramType = 0;
    paramName_ = paramName;
    defaultValue_ = def;
    maxValue_ = max;
    minValue_ = min;
    filterIndex_ = filterIndex;
    
    const auto name = from_u8(uiName);
    label_->setText(name);
    slider_->setAccessibleName(name);
    editor_->setAccessibleName(name);
    setFloatValue(defaultValue_);
}

void DynamicParamUIItem::setParam(const std::u8string& uiName, const std::u8string& paramName, int def, int max, int min, uint32_t filterIndex) {
    paramType = 1;
    paramName_ = paramName;
    defaultValue_ = def;
    maxValue_ = max;
    minValue_ = min;
    filterIndex_ = filterIndex;
    
    const auto name = from_u8(uiName);
    label_->setText(name);
    slider_->setAccessibleName(name);
    editor_->setAccessibleName(name);
    setFloatValue(defaultValue_);
}

void DynamicParamUI::create() {
    layout_ = new QVBoxLayout;
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(14);
    for (auto i = 0; i < kParamUINumber; i++) {
        items_[i].create();
        layout_->addLayout(items_[i].layout_);
    }
}

void DynamicParamUI::show(int howMany) {
    assert(howMany <= kParamUINumber);
    layout_->setSpacing(14);
    for (auto i = 0; i < howMany; i++) {
        items_[i].show();
    }
    
    used_ = howMany;
}

void DynamicParamUI::hide() {
    layout_->setSpacing(0);
    for (auto i = 0; i < kParamUINumber; i++) {
        items_[i].clear();
        items_[i].hide();
    }
    
    used_ = 0;
}

void DynamicParamUI::setEnabled(bool enabled) {
    for (auto& item : items_) {
        item.setEnabled(enabled);
    }
}

void DynamicParamUI::setParam(uint32_t idx, const std::u8string& uiName, const std::u8string& paramName, float def, float max, float min, uint32_t filterIndex) {
    items_[idx].setParam(uiName, paramName, def, max, min, filterIndex);
}

void DynamicParamUI::setParam(uint32_t idx, const std::u8string& uiName, const std::u8string& paramName, int def, int max, int min, uint32_t filterIndex) {
    items_[idx].setParam(uiName, paramName, def, max, min, filterIndex);
}
