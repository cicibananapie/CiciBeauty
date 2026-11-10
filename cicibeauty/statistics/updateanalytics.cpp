// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "updateanalytics.h"

#include <cassert>
#include <string>

#include "eventid.h"
#include "nlohmann/json.hpp"
#include "cicireport.h"

namespace {

const char8_t* updateTypeName(UpdateType type) {
    switch (type) {
        case UpdateType::NoNeedUpdate:
            return u8"none";
        case UpdateType::SilentUpdate:
            return u8"silent";
        case UpdateType::PromptedForegroundUpdate:
            return u8"prompted_foreground";
        case UpdateType::PromptedBackgroundUpdate:
            return u8"prompted_background";
        case UpdateType::ForceUpdate:
            return u8"forced";
    }

    assert(false);
    return u8"unknown";
}

const char8_t* checkResultName(UpdateCheckResult result) {
    switch (result) {
        case UpdateCheckResult::NoUpdate:
            return u8"no_update";
        case UpdateCheckResult::UpdateAvailable:
            return u8"update_available";
        case UpdateCheckResult::Failed:
            return u8"failed";
    }

    assert(false);
    return u8"unknown";
}

const char8_t* uiActionName(UpdateUiAction action) {
    switch (action) {
        case UpdateUiAction::DialogShow:
            return u8"dialog_show";
        case UpdateUiAction::StartClick:
            return u8"start_click";
        case UpdateUiAction::RetryClick:
            return u8"retry_click";
        case UpdateUiAction::DismissClick:
            return u8"dismiss_click";
        case UpdateUiAction::CancelClick:
            return u8"cancel_click";
        case UpdateUiAction::RestartClick:
            return u8"restart_click";
        case UpdateUiAction::CloseClick:
            return u8"close_click";
    }

    assert(false);
    return u8"unknown";
}

const char8_t* updateResultName(UpdateResult result) {
    switch (result) {
        case UpdateResult::Succeeded:
            return u8"succeeded";
        case UpdateResult::Failed:
            return u8"failed";
        case UpdateResult::Cancelled:
            return u8"cancelled";
        case UpdateResult::Interrupted:
            return u8"interrupted";
        case UpdateResult::StartRequestFailed:
            return u8"start_request_failed";
    }

    assert(false);
    return u8"unknown";
}

nlohmann::json baseProperties(UpdateType type,
                              std::u8string_view targetVersion) {
    return nlohmann::json::object({
        {"update_type", to_json(std::u8string(updateTypeName(type)))},
        {"target_version", to_json(std::u8string(targetVersion))},
    });
}

} // namespace

void reportUpdateCheckResult(UpdateCheckResult result,
                             UpdateType type,
                             std::u8string_view targetVersion) {
    auto properties = baseProperties(type, targetVersion);
    properties["result"] = to_json(std::u8string(checkResultName(result)));
    CiciReport::reportEvent(KEVT_APP_UPDATE_CHECK_RESULT, properties);
}

void reportUpdateUiAction(UpdateUiAction action,
                          UpdateType type,
                          std::u8string_view targetVersion) {
    auto properties = baseProperties(type, targetVersion);
    properties["action"] = to_json(std::u8string(uiActionName(action)));
    CiciReport::reportEvent(KEVT_APP_UPDATE_UI_ACTION, properties);
}

void reportUpdateStart(UpdateType type,
                       std::u8string_view targetVersion,
                       uint32_t attempt) {
    auto properties = baseProperties(type, targetVersion);
    properties["trigger"] = to_json(std::u8string(
        type == UpdateType::SilentUpdate ? u8"automatic" : u8"user"));
    properties["attempt"] = attempt;
    CiciReport::reportEvent(KEVT_APP_UPDATE_START, properties);
}

void reportUpdateResult(UpdateResult result,
                        UpdateType type,
                        std::u8string_view targetVersion,
                        uint32_t attempt) {
    auto properties = baseProperties(type, targetVersion);
    properties["result"] = to_json(std::u8string(updateResultName(result)));
    properties["attempt"] = attempt;
    CiciReport::reportEvent(KEVT_APP_UPDATE_RESULT, properties);
}
