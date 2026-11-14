// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#pragma once

#include "message.h"

namespace cici::protocol {

// Settings asynchronous requests:
// getSnapshot {} -> {uid, defaults, favorites, configs};
// getConfig {uid, instanceId} -> {config}; getFavorites {uid} -> {favorites};
// replace {uid, instanceId, config} -> {config};
// replaceFavorites {uid, favorites} -> {favorites}.
// patch {uid, instanceId, patch} -> {config};
// setFavorite {uid, page, effectId, favorite} -> {favorites}.
// Configuration data is returned only in request replies; reads in the UI use the local mirror.
// Mutations carry the cached uid so queued work cannot change another account.
//
// Each instance owns its configuration, keyed by instanceId.
// Instance posts: create {instanceId, videoChannel, sourceUuid?, sourceType?}, destroy {instanceId},
// setMode {instanceId, mode: 1(horizontal)/2(vertical)}, retryPackage {instanceId, effectId}.
// Instance calls: queryParamValues {instanceId, paths, defaultOnly?} -> {values};
// canActivateEffect {instanceId, effectId} -> {allowed, disablePatch}.
// disablePatch contains add operations to disable conflicts; the query does not mutate settings.

// OBS supplies the Cici filter UUID as instanceId.
// sourceUuid is parent-source metadata and is not used to identify configurations.
inline bool readSettingsKey(const nlohmann::json& body, const char* field, std::u8string& id) {
    const auto it = body.find(field);
    if (it == body.end() || !it->is_string()) {
        return false;
    }
    const auto& key = it->get_ref<const std::string&>();
    if (key.empty() || key.size() > 128) {
        return false;
    }
    for (unsigned char c : key) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-')) {
            return false;
        }
    }
    id = to_u8(key);
    return true;
}

inline JsonMessage instanceMessage(std::string_view method, std::u8string_view id,
                                   nlohmann::json body = nlohmann::json::object()) {
    body["instanceId"] = to_json(std::u8string(id));
    return JsonMessage(method, std::move(body));
}

} // namespace cici::protocol
