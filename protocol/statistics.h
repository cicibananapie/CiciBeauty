// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#pragma once

#include "message.h"

namespace cici::protocol {

inline JsonMessage statisticsReport(std::u8string_view event, const nlohmann::json& properties) {
    return JsonMessage("statistics.report",
                       {
                           {"event", std::string(reinterpret_cast<const char*>(event.data()), event.size())},
                           {"properties", properties},
                       });
}

} // namespace cici::protocol
