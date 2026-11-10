// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "loginanalytics.h"

#include <cassert>
#include <string>

#include "eventid.h"
#include "nlohmann/json.hpp"
#include "cicireport.h"

namespace {

const char8_t* surfaceName(LoginUiSurface surface) {
    switch (surface) {
        case LoginUiSurface::Modal:
            return u8"modal";
        case LoginUiSurface::SettingsOverlay:
            return u8"settings_overlay";
    }

    assert(false);
    return u8"unknown";
}

const char8_t* actionName(LoginUiAction action) {
    switch (action) {
        case LoginUiAction::Show:
            return u8"show";
        case LoginUiAction::SignInClick:
            return u8"sign_in_click";
        case LoginUiAction::Close:
            return u8"close";
    }

    assert(false);
    return u8"unknown";
}

} // namespace

void reportLoginUiAction(LoginUiSurface surface, LoginUiAction action) {
    CiciReport::reportEvent(KEVT_APP_LOGIN_UI_ACTION,
                            nlohmann::json::object({
                                {"surface", to_json(std::u8string(surfaceName(surface)))},
                                {"action", to_json(std::u8string(actionName(action)))},
                            }));
}
