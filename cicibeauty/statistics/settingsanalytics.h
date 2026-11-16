// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

class GlobalModel;

#include <cstdint>
#include <string>
#include <string_view>

enum class SettingsTab {
    Basic,
    Filter,
    BeautyEnhancement,
    Makeup,
    BodyShaping,
    AiEffect,
    EffectAsset,
    Keying,
    LensEffect,
    ImportExport,
};

enum class SettingsOpenEntry {
    ToolsMenu,
    ContextBar,
    DocksMenu,
};

enum class AccountUiAction {
    ManageAccountClick,
    LogoutClick,
};

enum class SupportUiAction {
    QqServiceClick,
    GuideClick,
    FeedbackClick,
};

enum class ConfigTransferUiAction {
    ImportClick,
    ExportClick,
};

enum class ConfigTransferResult {
    Succeeded,
    Failed,
    Cancelled,
};

enum class ConfigTransferFailureReason {
    None,
    FileDialogCancelled,
    InvalidFile,
    InvalidDocument,
    UserCancelled,
    ApplyFailed,
    NoActiveConfig,
    WriteFailed,
};

void reportSettingsTabSelect(GlobalModel* app, SettingsTab tab);
void reportSettingsOpen(GlobalModel* app, SettingsOpenEntry entry);
void reportSettingsSourceSelect(GlobalModel* app, std::u8string_view sourceUuid,
                                std::u8string_view sourceType,
                                std::u8string_view sourceName);
void reportAccountUiAction(GlobalModel* app, AccountUiAction action);
void reportSupportUiAction(GlobalModel* app, SupportUiAction action);
std::u8string reportConfigTransferUiAction(GlobalModel* app, ConfigTransferUiAction action);
void reportConfigTransferResult(GlobalModel* app, ConfigTransferUiAction action,
                                std::u8string_view attemptId,
                                ConfigTransferResult result,
                                ConfigTransferFailureReason failureReason,
                                uint64_t durationMs);
