// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <stdint.h>

#include <optional>
#include <string_view>

class GlobalModel;

enum class EffectPickerPage {
    AiEffect,
    EffectAsset,
};

void reportBasicFacePointsToggle(GlobalModel* app, bool enabled);

void reportFilterEnableToggle(GlobalModel* app, bool enabled);
void reportFilterViewSelect(GlobalModel* app, std::u8string_view view);
void reportFilterEffectSelect(GlobalModel* app,
                              uint64_t effectId,
                              std::u8string_view effectName,
                              bool selected,
                              std::u8string_view entryPoint);
void reportFilterFavoriteToggle(GlobalModel* app,
                                uint64_t effectId,
                                std::u8string_view effectName,
                                bool favorite);
void reportFilterStrengthInput(GlobalModel* app,
                               uint64_t effectId,
                               std::u8string_view effectName,
                               int value);

void reportMakeupEnableToggle(GlobalModel* app, bool enabled);
void reportMakeupTypeSelect(GlobalModel* app, std::u8string_view makeupType);
void reportMakeupEffectSelect(GlobalModel* app,
                              std::u8string_view makeupType,
                              std::optional<uint32_t> categoryId,
                              std::u8string_view categoryName,
                              uint64_t effectId,
                              std::u8string_view effectName,
                              bool selected);
void reportMakeupParamInput(GlobalModel* app,
                            std::u8string_view makeupType,
                            std::optional<uint32_t> categoryId,
                            std::u8string_view categoryName,
                            uint64_t effectId,
                            std::u8string_view effectName,
                            std::u8string_view param,
                            int value);
void reportMakeupStrengthInput(GlobalModel* app,
                               uint32_t categoryId,
                               std::u8string_view categoryName,
                               uint64_t effectId,
                               std::u8string_view effectName,
                               int value);

void reportEffectPickerSoundToggle(GlobalModel* app,
                                   EffectPickerPage page,
                                   bool enabled);
void reportEffectPickerCategorySelect(GlobalModel* app,
                                      EffectPickerPage page,
                                      uint32_t categoryId,
                                      std::u8string_view categoryName);
void reportEffectPickerEffectSelect(GlobalModel* app,
                                    EffectPickerPage page,
                                    uint32_t categoryId,
                                    std::u8string_view categoryName,
                                    std::optional<uint32_t> groupId,
                                    std::u8string_view groupName,
                                    uint64_t effectId,
                                    std::u8string_view effectName,
                                    bool selected,
                                    std::u8string_view entryPoint);
void reportEffectPickerFavoriteToggle(GlobalModel* app,
                                      EffectPickerPage page,
                                      uint64_t effectId,
                                      std::u8string_view effectName,
                                      bool favorite);

void reportBeautyEnhancementEnableToggle(GlobalModel* app, bool enabled);
void reportBeautyEnhancementSolutionAction(GlobalModel* app,
                                           std::u8string_view action,
                                           uint32_t solutionId,
                                           std::u8string_view solutionName);
void reportBeautyEnhancementFeatureToggle(GlobalModel* app,
                                          uint32_t solutionId,
                                          std::u8string_view solutionName,
                                          std::u8string_view feature,
                                          bool enabled);
void reportBeautyEnhancementSectionSelect(GlobalModel* app,
                                          uint32_t solutionId,
                                          std::u8string_view solutionName,
                                          std::u8string_view section);
void reportBeautyEnhancementParamInput(GlobalModel* app,
                                       uint32_t solutionId,
                                       std::u8string_view solutionName,
                                       std::u8string_view param,
                                       int value);

void reportBodyShapingEnableToggle(GlobalModel* app, bool enabled);
void reportBodyShapingSolutionAction(GlobalModel* app,
                                     std::u8string_view action,
                                     uint32_t solutionId,
                                     std::u8string_view solutionName);
void reportBodyShapingLegModeSelect(GlobalModel* app,
                                    uint32_t solutionId,
                                    std::u8string_view solutionName,
                                    std::u8string_view mode);
void reportBodyShapingParamInput(GlobalModel* app,
                                 uint32_t solutionId,
                                 std::u8string_view solutionName,
                                 std::u8string_view param,
                                 int value);
void reportBodyShapingManualLegAction(GlobalModel* app,
                                      uint32_t solutionId,
                                      std::u8string_view solutionName,
                                      std::u8string_view action);

void reportKeyingAction(GlobalModel* app, std::u8string_view action);
void reportKeyingToggle(GlobalModel* app,
                        std::u8string_view action,
                        bool enabled);
void reportKeyingModeSelect(GlobalModel* app, std::u8string_view mode);
void reportKeyingTemplateAction(GlobalModel* app,
                                std::u8string_view action,
                                std::u8string_view templateName);
void reportKeyingZoneShapeSelect(GlobalModel* app, std::u8string_view shape);
void reportKeyingBackgroundAssetSelect(GlobalModel* app,
                                       std::u8string_view assetType);
void reportKeyingTemplateParamInput(GlobalModel* app,
                                    std::u8string_view templateName,
                                    std::u8string_view param,
                                    int value);
void reportKeyingTemplateToggle(GlobalModel* app,
                                std::u8string_view templateName,
                                std::u8string_view feature,
                                bool enabled);

void reportLensViewSelect(GlobalModel* app, std::u8string_view view);
void reportLensToggle(GlobalModel* app,
                      std::u8string_view action,
                      bool enabled);
void reportLensEffectSelect(GlobalModel* app,
                            std::u8string_view section,
                            uint64_t effectId,
                            std::u8string_view effectName,
                            bool selected,
                            std::u8string_view entryPoint);
void reportLensFavoriteToggle(GlobalModel* app,
                              uint64_t effectId,
                              std::u8string_view effectName,
                              bool favorite);
void reportLensCategorySelect(GlobalModel* app,
                              uint32_t categoryId,
                              std::u8string_view categoryName);
void reportLensParamInput(GlobalModel* app,
                          std::u8string_view section,
                          uint64_t effectId,
                          std::u8string_view effectName,
                          std::u8string_view param,
                          int value);
