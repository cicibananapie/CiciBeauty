// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "settingsanalytics.h"

#include <cassert>

#include "eventid.h"
#include "nlohmann/json.hpp"
#include "cicireport.h"
#include "uuid.h"

namespace {

const char8_t* settingsTabName(SettingsTab tab) {
    switch (tab) {
        case SettingsTab::Basic:
            return u8"basic";
        case SettingsTab::Filter:
            return u8"filter";
        case SettingsTab::BeautyEnhancement:
            return u8"beauty_enhancement";
        case SettingsTab::Makeup:
            return u8"makeup";
        case SettingsTab::BodyShaping:
            return u8"body_shaping";
        case SettingsTab::AiEffect:
            return u8"ai_effect";
        case SettingsTab::EffectAsset:
            return u8"effect_asset";
        case SettingsTab::Keying:
            return u8"keying";
        case SettingsTab::LensEffect:
            return u8"lens_effect";
        case SettingsTab::ImportExport:
            return u8"import_export";
    }

    assert(false);
    return u8"unknown";
}

const char8_t* settingsOpenEntryName(SettingsOpenEntry entry) {
    switch (entry) {
        case SettingsOpenEntry::ToolsMenu:
            return u8"tools_menu";
        case SettingsOpenEntry::ContextBar:
            return u8"context_bar";
        case SettingsOpenEntry::DocksMenu:
            return u8"docks_menu";
    }

    assert(false);
    return u8"unknown";
}

const char8_t* accountActionName(AccountUiAction action) {
    switch (action) {
        case AccountUiAction::ManageAccountClick:
            return u8"manage_account_click";
        case AccountUiAction::LogoutClick:
            return u8"logout_click";
    }

    assert(false);
    return u8"unknown";
}

const char8_t* supportActionName(SupportUiAction action) {
    switch (action) {
        case SupportUiAction::QqServiceClick:
            return u8"qq_service_click";
        case SupportUiAction::GuideClick:
            return u8"guide_click";
        case SupportUiAction::FeedbackClick:
            return u8"feedback_click";
    }

    assert(false);
    return u8"unknown";
}

const char8_t* configTransferActionName(ConfigTransferUiAction action) {
    switch (action) {
        case ConfigTransferUiAction::ImportClick:
            return u8"import_click";
        case ConfigTransferUiAction::ExportClick:
            return u8"export_click";
    }

    assert(false);
    return u8"unknown";
}

const char8_t* configTransferOperationName(ConfigTransferUiAction action) {
    switch (action) {
        case ConfigTransferUiAction::ImportClick:
            return u8"import";
        case ConfigTransferUiAction::ExportClick:
            return u8"export";
    }

    assert(false);
    return u8"unknown";
}

const char8_t* configTransferResultName(ConfigTransferResult result) {
    switch (result) {
        case ConfigTransferResult::Succeeded:
            return u8"succeeded";
        case ConfigTransferResult::Failed:
            return u8"failed";
        case ConfigTransferResult::Cancelled:
            return u8"cancelled";
    }

    assert(false);
    return u8"unknown";
}

const char8_t* configTransferFailureReasonName(ConfigTransferFailureReason reason) {
    switch (reason) {
        case ConfigTransferFailureReason::None:
            return u8"";
        case ConfigTransferFailureReason::FileDialogCancelled:
            return u8"file_dialog_cancelled";
        case ConfigTransferFailureReason::InvalidFile:
            return u8"invalid_file";
        case ConfigTransferFailureReason::InvalidDocument:
            return u8"invalid_document";
        case ConfigTransferFailureReason::UserCancelled:
            return u8"user_cancelled";
        case ConfigTransferFailureReason::ApplyFailed:
            return u8"apply_failed";
        case ConfigTransferFailureReason::NoActiveConfig:
            return u8"no_active_config";
        case ConfigTransferFailureReason::WriteFailed:
            return u8"write_failed";
    }

    assert(false);
    return u8"unknown";
}

void reportAction(const char8_t* eventId, const char8_t* action) {
    CiciReport::reportEvent(eventId, nlohmann::json::object({
        {"action", to_json(std::u8string(action))},
    }));
}

} // namespace

void reportSettingsTabSelect(SettingsTab tab) {
    CiciReport::reportEvent(KEVT_APP_SETTINGS_TAB_SELECT,
                            nlohmann::json::object({
                                {"tab", to_json(std::u8string(settingsTabName(tab)))},
                            }));
}

void reportSettingsOpen(SettingsOpenEntry entry) {
    CiciReport::reportEvent(KEVT_APP_SETTINGS_OPEN,
                            nlohmann::json::object({
                                {"entry", to_json(std::u8string(settingsOpenEntryName(entry)))},
                            }));
}

void reportSettingsSourceSelect(std::u8string_view sourceUuid,
                                std::u8string_view sourceType,
                                std::u8string_view sourceName) {
    CiciReport::reportEvent(KEVT_APP_SETTINGS_SOURCE_SELECT,
                            nlohmann::json::object({
                                {"source_uuid", to_json(std::u8string(sourceUuid))},
                                {"source_type", to_json(std::u8string(sourceType))},
                                {"source_name", to_json(std::u8string(sourceName))},
                            }));
}

void reportAccountUiAction(AccountUiAction action) {
    reportAction(KEVT_APP_ACCOUNT_UI_ACTION, accountActionName(action));
}

void reportSupportUiAction(SupportUiAction action) {
    reportAction(KEVT_APP_SUPPORT_UI_ACTION, supportActionName(action));
}

std::u8string reportConfigTransferUiAction(ConfigTransferUiAction action) {
    const std::u8string attemptId = CiciUUID::generate();
    CiciReport::reportEvent(KEVT_APP_CONFIG_TRANSFER_UI_ACTION,
                            nlohmann::json::object({
                                {"action", to_json(std::u8string(
                                    configTransferActionName(action)))},
                                {"attempt_id", to_json(attemptId)},
                            }));
    return attemptId;
}

void reportConfigTransferResult(ConfigTransferUiAction action,
                                std::u8string_view attemptId,
                                ConfigTransferResult result,
                                ConfigTransferFailureReason failureReason,
                                uint64_t durationMs) {
    CiciReport::reportEvent(KEVT_APP_CONFIG_TRANSFER_RESULT,
                            nlohmann::json::object({
                                {"attempt_id", to_json(std::u8string(attemptId))},
                                {"operation", to_json(std::u8string(
                                    configTransferOperationName(action)))},
                                {"result", to_json(std::u8string(
                                    configTransferResultName(result)))},
                                {"failure_reason", to_json(std::u8string(
                                    configTransferFailureReasonName(failureReason)))},
                                {"duration_ms", durationMs},
                            }));
}
