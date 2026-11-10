// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#pragma once

#include "message.h"

namespace cici::protocol {

inline JsonMessage startFeedback(std::u8string_view ipcName) {
    return JsonMessage("helpers.startFeedback", {{"ipcName", to_json(std::u8string(ipcName))}});
}

inline JsonMessage startUpdater(std::u8string_view ipcName, std::u8string_view version,
                                std::u8string_view locale) {
    return JsonMessage("helpers.startUpdater", {{"ipcName", to_json(std::u8string(ipcName))},
        {"version", to_json(std::u8string(version))}, {"locale", to_json(std::u8string(locale))}});
}

inline JsonMessage helperStartFailed(std::string_view helper, std::u8string_view ipcName) {
    return JsonMessage("helpers.startFailed", {{"helper", helper}, {"ipcName", to_json(std::u8string(ipcName))}});
}

} // namespace cici::protocol
