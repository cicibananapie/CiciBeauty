// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "settingspageanalytics.h"

#include <cassert>
#include <utility>

#include "eventid.h"
#include "nlohmann/json.hpp"
#include "cicireport.h"
#include "../modelapp.h"

namespace {

nlohmann::json jsonText(std::u8string_view value) {
    return to_json(std::u8string(value));
}

nlohmann::json baseProperties(GlobalModel* app, std::u8string_view action) {
    assert(app != nullptr);
    auto* model = app != nullptr ? app->filterModel() : nullptr;
    return nlohmann::json::object({
        {"source_uuid", model != nullptr ? to_json(model->sourceUuid()) : to_json(std::u8string())},
        {"action", jsonText(action)},
    });
}

void report(const char8_t* eventId, nlohmann::json properties) {
    CiciReport::reportEvent(eventId, properties);
}

const char8_t* effectPickerEventId(EffectPickerPage page) {
    switch (page) {
        case EffectPickerPage::AiEffect:
            return KEVT_APP_AI_EFFECT_UI_ACTION;
        case EffectPickerPage::EffectAsset:
            return KEVT_APP_EFFECT_ASSET_UI_ACTION;
    }

    assert(false);
    return KEVT_APP_AI_EFFECT_UI_ACTION;
}

}

void reportBasicFacePointsToggle(GlobalModel* app, bool enabled) {
    auto properties = baseProperties(app, u8"face_points_toggle");
    properties["enabled"] = enabled;
    report(KEVT_APP_BASIC_SETTINGS_UI_ACTION, std::move(properties));
}

void reportFilterEnableToggle(GlobalModel* app, bool enabled) {
    auto properties = baseProperties(app, u8"enable_toggle");
    properties["enabled"] = enabled;
    report(KEVT_APP_FILTER_UI_ACTION, std::move(properties));
}

void reportFilterViewSelect(GlobalModel* app, std::u8string_view view) {
    auto properties = baseProperties(app, u8"view_select");
    properties["view"] = jsonText(view);
    report(KEVT_APP_FILTER_UI_ACTION, std::move(properties));
}

void reportFilterEffectSelect(GlobalModel* app,
                              uint64_t effectId,
                              std::u8string_view effectName,
                              bool selected,
                              std::u8string_view entryPoint) {
    auto properties = baseProperties(app, u8"effect_select");
    properties["effect_id"] = effectId;
    properties["effect_name"] = jsonText(effectName);
    properties["selected"] = selected;
    properties["entry_point"] = jsonText(entryPoint);
    report(KEVT_APP_FILTER_UI_ACTION, std::move(properties));
}

void reportFilterFavoriteToggle(GlobalModel* app,
                                uint64_t effectId,
                                std::u8string_view effectName,
                                bool favorite) {
    auto properties = baseProperties(app, u8"favorite_toggle");
    properties["effect_id"] = effectId;
    properties["effect_name"] = jsonText(effectName);
    properties["favorite"] = favorite;
    report(KEVT_APP_FILTER_UI_ACTION, std::move(properties));
}

void reportFilterStrengthInput(GlobalModel* app,
                               uint64_t effectId,
                               std::u8string_view effectName,
                               int value) {
    auto properties = baseProperties(app, u8"strength_input");
    properties["effect_id"] = effectId;
    properties["effect_name"] = jsonText(effectName);
    properties["value"] = value;
    report(KEVT_APP_FILTER_UI_ACTION, std::move(properties));
}

void reportMakeupEnableToggle(GlobalModel* app, bool enabled) {
    auto properties = baseProperties(app, u8"enable_toggle");
    properties["enabled"] = enabled;
    report(KEVT_APP_MAKEUP_UI_ACTION, std::move(properties));
}

void reportMakeupTypeSelect(GlobalModel* app, std::u8string_view makeupType) {
    auto properties = baseProperties(app, u8"type_select");
    properties["makeup_type"] = jsonText(makeupType);
    report(KEVT_APP_MAKEUP_UI_ACTION, std::move(properties));
}

void reportMakeupEffectSelect(GlobalModel* app,
                              std::u8string_view makeupType,
                              std::optional<uint32_t> categoryId,
                              std::u8string_view categoryName,
                              uint64_t effectId,
                              std::u8string_view effectName,
                              bool selected) {
    auto properties = baseProperties(app, u8"effect_select");
    properties["makeup_type"] = jsonText(makeupType);
    if (categoryId.has_value()) {
        properties["category_id"] = *categoryId;
        properties["category_name"] = jsonText(categoryName);
    }
    properties["effect_id"] = effectId;
    properties["effect_name"] = jsonText(effectName);
    properties["selected"] = selected;
    report(KEVT_APP_MAKEUP_UI_ACTION, std::move(properties));
}

void reportMakeupParamInput(GlobalModel* app,
                            std::u8string_view makeupType,
                            std::optional<uint32_t> categoryId,
                            std::u8string_view categoryName,
                            uint64_t effectId,
                            std::u8string_view effectName,
                            std::u8string_view param,
                            int value) {
    auto properties = baseProperties(app, u8"param_input");
    properties["makeup_type"] = jsonText(makeupType);
    if (categoryId.has_value()) {
        properties["category_id"] = *categoryId;
        properties["category_name"] = jsonText(categoryName);
    }
    properties["effect_id"] = effectId;
    properties["effect_name"] = jsonText(effectName);
    properties["param"] = jsonText(param);
    properties["value"] = value;
    report(KEVT_APP_MAKEUP_UI_ACTION, std::move(properties));
}

void reportMakeupStrengthInput(GlobalModel* app,
                               uint32_t categoryId,
                               std::u8string_view categoryName,
                               uint64_t effectId,
                               std::u8string_view effectName,
                               int value) {
    auto properties = baseProperties(app, u8"strength_input");
    properties["makeup_type"] = to_json(std::u8string(u8"customized"));
    properties["category_id"] = categoryId;
    properties["category_name"] = jsonText(categoryName);
    properties["effect_id"] = effectId;
    properties["effect_name"] = jsonText(effectName);
    properties["value"] = value;
    report(KEVT_APP_MAKEUP_UI_ACTION, std::move(properties));
}

void reportEffectPickerSoundToggle(GlobalModel* app,
                                   EffectPickerPage page,
                                   bool enabled) {
    auto properties = baseProperties(app, u8"sound_toggle");
    properties["enabled"] = enabled;
    report(effectPickerEventId(page), std::move(properties));
}

void reportEffectPickerCategorySelect(GlobalModel* app,
                                      EffectPickerPage page,
                                      uint32_t categoryId,
                                      std::u8string_view categoryName) {
    auto properties = baseProperties(app, u8"category_select");
    properties["category_id"] = categoryId;
    properties["category_name"] = jsonText(categoryName);
    report(effectPickerEventId(page), std::move(properties));
}

void reportEffectPickerEffectSelect(GlobalModel* app,
                                    EffectPickerPage page,
                                    uint32_t categoryId,
                                    std::u8string_view categoryName,
                                    std::optional<uint32_t> groupId,
                                    std::u8string_view groupName,
                                    uint64_t effectId,
                                    std::u8string_view effectName,
                                    bool selected,
                                    std::u8string_view entryPoint) {
    auto properties = baseProperties(app, u8"effect_select");
    properties["category_id"] = categoryId;
    properties["category_name"] = jsonText(categoryName);
    if (groupId.has_value()) {
        properties["group_id"] = *groupId;
        properties["group_name"] = jsonText(groupName);
    }
    properties["effect_id"] = effectId;
    properties["effect_name"] = jsonText(effectName);
    properties["selected"] = selected;
    properties["entry_point"] = jsonText(entryPoint);
    report(effectPickerEventId(page), std::move(properties));
}

void reportEffectPickerFavoriteToggle(GlobalModel* app,
                                      EffectPickerPage page,
                                      uint64_t effectId,
                                      std::u8string_view effectName,
                                      bool favorite) {
    auto properties = baseProperties(app, u8"favorite_toggle");
    properties["effect_id"] = effectId;
    properties["effect_name"] = jsonText(effectName);
    properties["favorite"] = favorite;
    report(effectPickerEventId(page), std::move(properties));
}

void reportBeautyEnhancementEnableToggle(GlobalModel* app, bool enabled) {
    auto properties = baseProperties(app, u8"enable_toggle");
    properties["enabled"] = enabled;
    report(KEVT_APP_BEAUTY_ENHANCEMENT_UI_ACTION, std::move(properties));
}

void reportBeautyEnhancementSolutionAction(GlobalModel* app,
                                           std::u8string_view action,
                                           uint32_t solutionId,
                                           std::u8string_view solutionName) {
    auto properties = baseProperties(app, action);
    properties["solution_id"] = solutionId;
    properties["solution_name"] = jsonText(solutionName);
    report(KEVT_APP_BEAUTY_ENHANCEMENT_UI_ACTION, std::move(properties));
}

void reportBeautyEnhancementFeatureToggle(GlobalModel* app,
                                          uint32_t solutionId,
                                          std::u8string_view solutionName,
                                          std::u8string_view feature,
                                          bool enabled) {
    auto properties = baseProperties(app, u8"feature_toggle");
    properties["solution_id"] = solutionId;
    properties["solution_name"] = jsonText(solutionName);
    properties["feature"] = jsonText(feature);
    properties["enabled"] = enabled;
    report(KEVT_APP_BEAUTY_ENHANCEMENT_UI_ACTION, std::move(properties));
}

void reportBeautyEnhancementSectionSelect(GlobalModel* app,
                                          uint32_t solutionId,
                                          std::u8string_view solutionName,
                                          std::u8string_view section) {
    auto properties = baseProperties(app, u8"section_select");
    properties["solution_id"] = solutionId;
    properties["solution_name"] = jsonText(solutionName);
    properties["section"] = jsonText(section);
    report(KEVT_APP_BEAUTY_ENHANCEMENT_UI_ACTION, std::move(properties));
}

void reportBeautyEnhancementParamInput(GlobalModel* app,
                                       uint32_t solutionId,
                                       std::u8string_view solutionName,
                                       std::u8string_view param,
                                       int value) {
    auto properties = baseProperties(app, u8"param_input");
    properties["solution_id"] = solutionId;
    properties["solution_name"] = jsonText(solutionName);
    properties["param"] = jsonText(param);
    properties["value"] = value;
    report(KEVT_APP_BEAUTY_ENHANCEMENT_UI_ACTION, std::move(properties));
}

void reportBodyShapingEnableToggle(GlobalModel* app, bool enabled) {
    auto properties = baseProperties(app, u8"enable_toggle");
    properties["enabled"] = enabled;
    report(KEVT_APP_BODY_SHAPING_UI_ACTION, std::move(properties));
}

void reportBodyShapingSolutionAction(GlobalModel* app,
                                     std::u8string_view action,
                                     uint32_t solutionId,
                                     std::u8string_view solutionName) {
    auto properties = baseProperties(app, action);
    properties["solution_id"] = solutionId;
    properties["solution_name"] = jsonText(solutionName);
    report(KEVT_APP_BODY_SHAPING_UI_ACTION, std::move(properties));
}

void reportBodyShapingLegModeSelect(GlobalModel* app,
                                    uint32_t solutionId,
                                    std::u8string_view solutionName,
                                    std::u8string_view mode) {
    auto properties = baseProperties(app, u8"leg_mode_select");
    properties["solution_id"] = solutionId;
    properties["solution_name"] = jsonText(solutionName);
    properties["mode"] = jsonText(mode);
    report(KEVT_APP_BODY_SHAPING_UI_ACTION, std::move(properties));
}

void reportBodyShapingParamInput(GlobalModel* app,
                                 uint32_t solutionId,
                                 std::u8string_view solutionName,
                                 std::u8string_view param,
                                 int value) {
    auto properties = baseProperties(app, u8"param_input");
    properties["solution_id"] = solutionId;
    properties["solution_name"] = jsonText(solutionName);
    properties["param"] = jsonText(param);
    properties["value"] = value;
    report(KEVT_APP_BODY_SHAPING_UI_ACTION, std::move(properties));
}

void reportBodyShapingManualLegAction(GlobalModel* app,
                                      uint32_t solutionId,
                                      std::u8string_view solutionName,
                                      std::u8string_view action) {
    std::u8string fullAction = u8"manual_leg_";
    fullAction.append(action);
    auto properties = baseProperties(app, fullAction);
    properties["solution_id"] = solutionId;
    properties["solution_name"] = jsonText(solutionName);
    report(KEVT_APP_BODY_SHAPING_UI_ACTION, std::move(properties));
}

void reportKeyingAction(GlobalModel* app, std::u8string_view action) {
    auto properties = baseProperties(app, action);
    report(KEVT_APP_KEYING_UI_ACTION, std::move(properties));
}

void reportKeyingToggle(GlobalModel* app,
                        std::u8string_view action,
                        bool enabled) {
    auto properties = baseProperties(app, action);
    properties["enabled"] = enabled;
    report(KEVT_APP_KEYING_UI_ACTION, std::move(properties));
}

void reportKeyingModeSelect(GlobalModel* app, std::u8string_view mode) {
    auto properties = baseProperties(app, u8"mode_select");
    properties["mode"] = jsonText(mode);
    report(KEVT_APP_KEYING_UI_ACTION, std::move(properties));
}

void reportKeyingTemplateAction(GlobalModel* app,
                                std::u8string_view action,
                                std::u8string_view templateName) {
    auto properties = baseProperties(app, action);
    properties["template"] = jsonText(templateName);
    report(KEVT_APP_KEYING_UI_ACTION, std::move(properties));
}

void reportKeyingZoneShapeSelect(GlobalModel* app, std::u8string_view shape) {
    auto properties = baseProperties(app, u8"no_keying_zone_shape_select");
    properties["shape"] = jsonText(shape);
    report(KEVT_APP_KEYING_UI_ACTION, std::move(properties));
}

void reportKeyingBackgroundAssetSelect(GlobalModel* app,
                                       std::u8string_view assetType) {
    auto properties = baseProperties(app, u8"background_asset_select");
    properties["asset_type"] = jsonText(assetType);
    report(KEVT_APP_KEYING_UI_ACTION, std::move(properties));
}

void reportKeyingTemplateParamInput(GlobalModel* app,
                                    std::u8string_view templateName,
                                    std::u8string_view param,
                                    int value) {
    auto properties = baseProperties(app, u8"template_param_input");
    properties["template"] = jsonText(templateName);
    properties["param"] = jsonText(param);
    properties["value"] = value;
    report(KEVT_APP_KEYING_UI_ACTION, std::move(properties));
}

void reportKeyingTemplateToggle(GlobalModel* app,
                                std::u8string_view templateName,
                                std::u8string_view feature,
                                bool enabled) {
    auto properties = baseProperties(app, u8"template_feature_toggle");
    properties["template"] = jsonText(templateName);
    properties["feature"] = jsonText(feature);
    properties["enabled"] = enabled;
    report(KEVT_APP_KEYING_UI_ACTION, std::move(properties));
}

void reportLensViewSelect(GlobalModel* app, std::u8string_view view) {
    auto properties = baseProperties(app, u8"view_select");
    properties["view"] = jsonText(view);
    report(KEVT_APP_LENS_EFFECT_UI_ACTION, std::move(properties));
}

void reportLensToggle(GlobalModel* app,
                      std::u8string_view action,
                      bool enabled) {
    auto properties = baseProperties(app, action);
    properties["enabled"] = enabled;
    report(KEVT_APP_LENS_EFFECT_UI_ACTION, std::move(properties));
}

void reportLensEffectSelect(GlobalModel* app,
                            std::u8string_view section,
                            uint64_t effectId,
                            std::u8string_view effectName,
                            bool selected,
                            std::u8string_view entryPoint) {
    auto properties = baseProperties(app, u8"effect_select");
    properties["section"] = jsonText(section);
    properties["effect_id"] = effectId;
    properties["effect_name"] = jsonText(effectName);
    properties["selected"] = selected;
    properties["entry_point"] = jsonText(entryPoint);
    report(KEVT_APP_LENS_EFFECT_UI_ACTION, std::move(properties));
}

void reportLensFavoriteToggle(GlobalModel* app,
                              uint64_t effectId,
                              std::u8string_view effectName,
                              bool favorite) {
    auto properties = baseProperties(app, u8"favorite_toggle");
    properties["section"] = jsonText(u8"shot_atmosphere");
    properties["effect_id"] = effectId;
    properties["effect_name"] = jsonText(effectName);
    properties["favorite"] = favorite;
    report(KEVT_APP_LENS_EFFECT_UI_ACTION, std::move(properties));
}

void reportLensCategorySelect(GlobalModel* app,
                              uint32_t categoryId,
                              std::u8string_view categoryName) {
    auto properties = baseProperties(app, u8"category_select");
    properties["section"] = jsonText(u8"shot_atmosphere");
    properties["category_id"] = categoryId;
    properties["category_name"] = jsonText(categoryName);
    report(KEVT_APP_LENS_EFFECT_UI_ACTION, std::move(properties));
}

void reportLensParamInput(GlobalModel* app,
                          std::u8string_view section,
                          uint64_t effectId,
                          std::u8string_view effectName,
                          std::u8string_view param,
                          int value) {
    auto properties = baseProperties(app, u8"param_input");
    properties["section"] = jsonText(section);
    properties["effect_id"] = effectId;
    properties["effect_name"] = jsonText(effectName);
    properties["param"] = jsonText(param);
    properties["value"] = value;
    report(KEVT_APP_LENS_EFFECT_UI_ACTION, std::move(properties));
}
