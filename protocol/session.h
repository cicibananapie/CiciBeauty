// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#pragma once

#include <cstdint>
#include <string>

#include "message.h"

namespace cici::protocol {

struct HostInfo {
    std::string host;
    std::string hostVersion;
    uint32_t pid = 0;
    int64_t startedAtMs = 0;
    std::string pluginVersion;
    bool pluginOfficialBuild = false;
    std::string channelId;
};

inline JsonMessage initializeSession(std::string_view host, std::string_view hostVersion, uint32_t pid,
                                     int64_t startedAtMs, std::string_view pluginVersion,
                                     bool pluginOfficialBuild, std::string_view channelId) {
    return JsonMessage("session.initialize", {
                                                 {"host", host},
                                                 {"hostVersion", hostVersion},
                                                 {"pid", pid},
                                                 {"startedAtMs", startedAtMs},
                                                 {"pluginVersion", pluginVersion},
                                                 {"pluginOfficialBuild", pluginOfficialBuild},
                                                 {"channelId", channelId},
                                             });
}

inline bool readHostInfo(const nlohmann::json& body, HostInfo& info) {
    if (!body.is_object() || !body.contains("host") || !body["host"].is_string() ||
        body["host"].get_ref<const std::string&>().empty() || !body.contains("hostVersion") ||
        !body["hostVersion"].is_string() || !body.contains("pid") || !body["pid"].is_number_unsigned() ||
        body["pid"].get<uint64_t>() == 0 || body["pid"].get<uint64_t>() > UINT32_MAX || !body.contains("startedAtMs") ||
        !body["startedAtMs"].is_number_integer() ||
        (body["startedAtMs"].is_number_unsigned() && body["startedAtMs"].get<uint64_t>() > INT64_MAX) ||
        body["startedAtMs"].get<int64_t>() <= 0 ||
        !body.contains("pluginVersion") || !body["pluginVersion"].is_string() ||
        body["pluginVersion"].get_ref<const std::string&>().empty() ||
        !body.contains("pluginOfficialBuild") || !body["pluginOfficialBuild"].is_boolean() ||
        !body.contains("channelId") || !body["channelId"].is_string()) {
        return false;
    }
    info = {body["host"].get<std::string>(), body["hostVersion"].get<std::string>(), body["pid"].get<uint32_t>(),
            body["startedAtMs"].get<int64_t>(), body["pluginVersion"].get<std::string>(),
            body["pluginOfficialBuild"].get<bool>(), body["channelId"].get<std::string>()};
    return true;
}

} // namespace cici::protocol
