// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

#include "message.h"

struct EffectAsset {
    std::u8string url_;
    std::u8string sha256_;
    uint64_t size_ = 0;

    bool valid() const {
        return url_.starts_with(u8"https://") && size_ > 0 && sha256_.size() == 64 &&
               std::all_of(sha256_.begin(), sha256_.end(), [](char8_t ch) {
                   return (ch >= u8'0' && ch <= u8'9') || (ch >= u8'a' && ch <= u8'f');
               });
    }
};

struct EffectOperationContext {
    uint64_t effectId = 0;
    std::u8string sourceUuid;
    std::u8string filterUuid;
    std::u8string trigger;
    std::u8string packageSource;
    bool retry = false;
};

enum class EffectPackageState : uint8_t {
    Missing,
    Downloading,
    Ready,
    Failed,
    Unknown
};
enum class RemoteImageState : uint8_t {
    Loading,
    Ready,
    Failed
};

namespace cici::protocol {

inline nlohmann::json assetJson(const EffectAsset& asset) {
    return {{"url", to_json(asset.url_)}, {"sha256", to_json(asset.sha256_)}, {"size", asset.size_}};
}

inline bool readAsset(const nlohmann::json& value, EffectAsset& asset) {
    if (!value.is_object() || !value.contains("url") || !value["url"].is_string() || !value.contains("sha256") ||
        !value["sha256"].is_string() || !value.contains("size") || !value["size"].is_number_unsigned()) {
        return false;
    }
    asset = {to_u8(value["url"].get<std::string>()), to_u8(value["sha256"].get<std::string>()),
             value["size"].get<uint64_t>()};
    return asset.valid();
}

inline nlohmann::json assetContextJson(const EffectOperationContext& context) {
    return {{"effectId", context.effectId},
            {"sourceUuid", to_json(context.sourceUuid)},
            {"filterUuid", to_json(context.filterUuid)},
            {"trigger", to_json(context.trigger)},
            {"packageSource", to_json(context.packageSource)},
            {"retry", context.retry}};
}

inline bool readAssetContext(const nlohmann::json& value, EffectOperationContext& context) {
    if (!value.is_object() || !value.contains("effectId") || !value["effectId"].is_number_unsigned() ||
        !value.contains("retry") || !value["retry"].is_boolean()) {
        return false;
    }
    for (const char* field : {"sourceUuid", "filterUuid", "trigger", "packageSource"}) {
        if (!value.contains(field) || !value[field].is_string()) {
            return false;
        }
    }
    context = {value["effectId"].get<uint64_t>(),
               to_u8(value["sourceUuid"].get<std::string>()),
               to_u8(value["filterUuid"].get<std::string>()),
               to_u8(value["trigger"].get<std::string>()),
               to_u8(value["packageSource"].get<std::string>()),
               value["retry"].get<bool>()};
    return true;
}

template <class State> inline bool readAssetState(const nlohmann::json& value, State& state, State maximum) {
    if (!value.is_number_unsigned() || value.get<uint64_t>() > static_cast<uint8_t>(maximum)) {
        return false;
    }
    state = static_cast<State>(value.get<uint8_t>());
    return true;
}

inline JsonMessage requestRemoteImage(std::u8string_view url) {
    return {"assets.requestRemoteImage", {{"url", to_json(std::u8string(url))}}};
}

inline JsonMessage downloadEffectPackage(const EffectAsset& asset, const EffectOperationContext& context) {
    return {"assets.downloadEffectPackage", {{"package", assetJson(asset)}, {"context", assetContextJson(context)}}};
}

inline JsonMessage remoteImageStateChanged(std::u8string_view url, RemoteImageState state,
                                           const std::u8string& localPath) {
    return {"assets.remoteImageStateChanged",
            {{"url", to_json(std::u8string(url))},
             {"state", static_cast<uint8_t>(state)},
             {"localPath", to_json(localPath)}}};
}

inline JsonMessage effectPackageStateChanged(std::u8string_view hash, EffectPackageState state) {
    return {"assets.effectPackageStateChanged",
            {{"sha256", to_json(std::u8string(hash))}, {"state", static_cast<uint8_t>(state)}}};
}

} // namespace cici::protocol
