// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "settingconfig.h"
#include "lumaclient.h"
#include "jsonutility.h"
#include "protocol/settings.h"
#include "log.h"
#include <algorithm>

using namespace cici::protocol;
namespace {
constexpr auto kTimeout = std::chrono::milliseconds(100);
}

SettingConfig::SettingConfig() {
    LumaClient::instance().addListener(
        this,
        [this](const JsonMessage& message) {
            const auto& method = message.value["method"].get_ref<const std::string&>();
            const auto& body = message.value["body"];
            if (method == "settings.snapshot") {
                updateSnapshot(body);
            } else if (method == "settings.changed") {
                std::u8string id;
                if (readSettingsKey(body, "instanceId", id) && body.contains("uid") && body["uid"].is_string() &&
                    to_u8(body["uid"].get<std::string>()) == uid_ && body.contains("config") &&
                    body["config"].is_object()) {
                    configs_[id] = body["config"];
                }
            }
        },
        [this]() {
            uid_.clear();
            configs_.clear();
            defaults_ = {{"version", 1}};
            favorites_ = {{"version", 1}, {"pages", nlohmann::json::object()}};
        },
        [this]() {
            load();
        });
}
SettingConfig::~SettingConfig() {
    LumaClient::instance().removeListener(this);
}

void SettingConfig::updateSnapshot(const nlohmann::json& body) {
    if (!body.contains("uid") || !body["uid"].is_string() || !body.contains("defaults") ||
        !body["defaults"].is_object() || !body.contains("favorites") || !body["favorites"].is_object()) {
        return;
    }
    auto uid = to_u8(body["uid"].get<std::string>());
    if (uid != uid_) {
        configs_.clear();
    }
    uid_ = std::move(uid);
    defaults_ = body["defaults"];
    favorites_ = body["favorites"];
}

void SettingConfig::load() {
    const auto result = LumaClient::instance().call("settings.getSnapshot", nlohmann::json::object(), kTimeout);
    if (result.status == CallStatus::Success) {
        updateSnapshot(result.body);
    } else {
        LogWarn << "query settings snapshot failed, status=" << static_cast<int>(result.status) << logEnd;
    }
}
bool SettingConfig::post(std::string_view method, nlohmann::json body) const {
    body["uid"] = to_json(uid_);
    if (LumaClient::instance().postMsg(JsonMessage(method, std::move(body)))) {
        return true;
    }
    LogWarn << "send settings operation failed: " << method << logEnd;
    return false;
}
nlohmann::json SettingConfig::config(const std::u8string& id) const {
    if (id.empty()) {
        return defaults_;
    }
    const auto it = configs_.find(id);
    if (it != configs_.end()) {
        return it->second;
    }
    const auto result = LumaClient::instance().call("settings.getConfig", {{"instanceId", to_json(id)}}, kTimeout);
    if (result.status == CallStatus::Success && result.body.contains("config") && result.body["config"].is_object()) {
        return configs_[id] = result.body["config"];
    }
    LogWarn << "query settings failed, config_id=" << id << ", status=" << static_cast<int>(result.status) << logEnd;
    return defaults_;
}
bool SettingConfig::setConfig(const std::u8string& id, nlohmann::json& value) {
    const auto result = LumaClient::instance().call(
        "settings.replace", {{"uid", to_json(uid_)}, {"instanceId", to_json(id)}, {"config", value}}, kTimeout);
    if (result.status != CallStatus::Success || !result.body.contains("config") || !result.body["config"].is_object()) {
        LogWarn << "replace settings failed, config_id=" << id << ", status=" << static_cast<int>(result.status)
                << logEnd;
        return false;
    }
    value = result.body["config"];
    configs_[id] = value;
    return true;
}
void SettingConfig::applyPatch(const std::u8string& id, const nlohmann::json& patch) {
    if (uid_.empty()) {
        return;
    }
    auto next = config(id);
    if (!JsonUtility::Patch::apply(next, patch)) {
        LogWarn << "invalid settings patch, config_id=" << id << logEnd;
        return;
    }
    nlohmann::json body = {{"instanceId", to_json(id)}, {"patch", patch}};
    if (post("settings.patch", std::move(body))) {
        configs_[id] = std::move(next);
    }
}
bool SettingConfig::isFavorite(std::u8string_view page, uint64_t id) const {
    const auto pages = favorites_.find("pages");
    if (pages == favorites_.end() || !pages->is_object()) {
        return false;
    }
    const auto list = pages->find(to_char(std::u8string(page)));
    return list != pages->end() && list->is_array() && std::find(list->begin(), list->end(), id) != list->end();
}
bool SettingConfig::setFavorite(std::u8string_view page, uint64_t id, bool favorite) {
    if (uid_.empty() || page.empty() || !id) {
        return false;
    }
    if (isFavorite(page, id) == favorite) {
        return true;
    }
    if (!post("settings.setFavorite",
              {{"page", to_json(std::u8string(page))}, {"effectId", id}, {"favorite", favorite}})) {
        return false;
    }
    auto& list = favorites_["pages"][to_char(std::u8string(page))];
    if (!list.is_array()) {
        list = nlohmann::json::array();
    }
    if (favorite) {
        list.push_back(id);
    } else {
        list.erase(std::remove(list.begin(), list.end(), nlohmann::json(id)), list.end());
    }
    return true;
}
bool SettingConfig::replaceFavorites(const nlohmann::json& favorites) {
    const auto result = LumaClient::instance().call("settings.replaceFavorites",
                                                    {{"uid", to_json(uid_)}, {"favorites", favorites}}, kTimeout);
    if (result.status != CallStatus::Success || !result.body.contains("favorites") ||
        !result.body["favorites"].is_object()) {
        return false;
    }
    favorites_ = result.body["favorites"];
    return true;
}
