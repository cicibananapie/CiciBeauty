// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <stdint.h>
#include <string>
#include <vector>

class QLabel;
class QSlider;
class QLineEdit;
class QHBoxLayout;
class QVBoxLayout;

struct EffectDetailInfo {
    virtual ~EffectDetailInfo() = default;

    uint64_t effectId_ = 0;
    uint32_t filterIndex_ = 0;
    std::u8string paramName_;
    const char* jsonKey_ = nullptr;
    uint32_t value_ = 0;
    QSlider* slider_ = nullptr;
    QLineEdit* editor_ = nullptr;
};

/**
 EffectDetailInfo will be base class of DynamicParamUIItem in the future
 */
struct DynamicParamUIItem {
    QLabel* label_ = nullptr;
    QSlider* slider_ = nullptr;
    QLineEdit* editor_ = nullptr;
    QHBoxLayout* layout_ = nullptr;
    
    std::u8string paramName_;
    float defaultValue_ = 0;
    float maxValue_ = 0;
    float minValue_ = 0;
    uint32_t filterIndex_ = 0;
    uint32_t paramType = 0; //int, float...
    
    void create();
    void clear() {
        paramName_.clear();
        defaultValue_ = 0;
        maxValue_ = 0;
        minValue_ = 0;
        filterIndex_ = 0;
        paramType = 0;
    }
    
    void setParam(const std::u8string& uiName, const std::u8string& paramName, float def, float max, float min, uint32_t filterIndex);
    void setParam(const std::u8string &uiName, const std::u8string &paramName, int def, int max, int min, uint32_t filterIndex);
    
    float valueFromUI(int v, int min = 0, int max = 100) const {
        float f = (v - min)*1.0f/(max-min);
        
        return minValue_ + f*(maxValue_ - minValue_);
    }
    
    int valueToUI(float v, int min = 0, int max = 100) const {
        float f = (v - minValue_)/(maxValue_ - minValue_);
        
        return min + f*(max - min);
    }
    
    void setIntValue(uint32_t v);
    void setFloatValue(float f);
    void setEnabled(bool enabled);
    void show();
    void hide();
};

struct DynamicParamUI {
    static const uint32_t kParamUINumber = 11;

public:
    void create();
    void show(int howMany);
    void hide();
    void setEnabled(bool enabled);
    void setParam(uint32_t idx, const std::u8string &uiName, const std::u8string &paramName, float def, float max, float min, uint32_t filterIndex);
    void setParam(uint32_t idx, const std::u8string &uiName, const std::u8string &paramName, int def, int max, int min, uint32_t filterIndex);
    
    uint32_t effectId_ = 0;
    uint32_t used_ = 0;
    QVBoxLayout* layout_ = nullptr;
    DynamicParamUIItem items_[kParamUINumber];
};
