// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

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

void reportSettingsTabSelect(SettingsTab tab);
void reportSettingsOpen(SettingsOpenEntry entry);
void reportSettingsSourceSelect(std::u8string_view sourceUuid,
                                std::u8string_view sourceType,
                                std::u8string_view sourceName);
void reportAccountUiAction(AccountUiAction action);
void reportSupportUiAction(SupportUiAction action);
std::u8string reportConfigTransferUiAction(ConfigTransferUiAction action);
void reportConfigTransferResult(ConfigTransferUiAction action,
                                std::u8string_view attemptId,
                                ConfigTransferResult result,
                                ConfigTransferFailureReason failureReason,
                                uint64_t durationMs);
