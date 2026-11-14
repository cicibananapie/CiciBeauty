// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "settingconfig.h"
#include "jsonutility.h"
#include "protocol/account.h"
#include "protocol/settings.h"
#include "log.h"
#include <algorithm>

using namespace cici::protocol;

SettingConfig::SettingConfig() {
    LumaClient::instance().addListener(
        this,
        [this](const JsonMessage& message) {
            const auto& method = message.value["method"].get_ref<const std::string&>();
            const auto& body = message.value["body"];
            if (method == "login.ready" || method == "login.loggedOut" || method == "login.failed") {
                LoginInfo info;
                if (!readLoginInfo(body, info)) {
                    return;
                }
                const auto uid = info.hasUsableSession() ? info.uid : std::u8string();
                if (!ready_ || uid != uid_) {
                    requestSnapshot();
                } else {
                    for (const auto& [id, value] : configs_) {
                        refreshConfig(id);
                    }
                }
            } else if (method == "subscription.updated" && ready_ && body.contains("uid") &&
                       body["uid"] == to_json(uid_)) {
                // Restoring permission may normalize conflicting effects.
                for (const auto& [id, value] : configs_) {
                    refreshConfig(id);
                }
            }
        },
        [this]() {
            requests_.reset();
            ready_ = false;
            uid_.clear();
            configs_.clear();
            defaults_ = {{"version", 1}};
            favorites_ = {{"version", 1}, {"pages", nlohmann::json::object()}};
            emit invalidated();
        },
        [this]() {
            requestSnapshot();
        });
}
SettingConfig::~SettingConfig() {
    LumaClient::instance().removeListener(this);
}

void SettingConfig::requestSnapshot() {
    requests_.reset();
    ready_ = false;
    emit invalidated();
    requests_.callAsync(u8"snapshot", "settings.getSnapshot", nlohmann::json::object(), [this](CallResult result) {
        if (result.ok()) {
            updateSnapshot(result.body);
        } else {
            LogWarn << "query settings snapshot failed, status=" << static_cast<int>(result.status) << logEnd;
        }
    });
}

void SettingConfig::updateSnapshot(const nlohmann::json& body) {
    if (!body.contains("uid") || !body["uid"].is_string() || !body.contains("defaults") ||
        !body["defaults"].is_object() || !body.contains("favorites") || !body["favorites"].is_object() ||
        !body.contains("configs") || !body["configs"].is_object()) {
        LogWarn << "invalid settings snapshot" << logEnd;
        return;
    }
    std::map<std::u8string, nlohmann::json> configs;
    for (const auto& [id, value] : body["configs"].items()) {
        std::u8string key;
        if (!readSettingsKey({{"instanceId", id}}, "instanceId", key) || !value.is_object()) {
            LogWarn << "invalid instance in settings snapshot" << logEnd;
            return;
        }
        configs.emplace(std::move(key), value);
    }
    uid_ = to_u8(body["uid"].get<std::string>());
    configs_ = std::move(configs);
    defaults_ = body["defaults"];
    favorites_ = body["favorites"];
    ready_ = true;
    emit ready();
    emit favoritesChanged();
}

nlohmann::json SettingConfig::config(const std::u8string& id) const {
    const auto it = configs_.find(id);
    return it == configs_.end() ? defaults_ : it->second;
}

void SettingConfig::applyConfig(const std::u8string& id, const nlohmann::json& value,
                                const nlohmann::json& expected) {
    configs_[id] = value;
    if (value != expected) {
        emit configAdjusted(id);
    }
}

void SettingConfig::refreshConfig(const std::u8string& id) {
    if (!ready_ || id.empty()) {
        return;
    }
    requests_.callAsync(u8"config:" + id, "settings.getConfig", {{"uid", to_json(uid_)}, {"instanceId", to_json(id)}},
        [this, id, expected = config(id)](CallResult result) {
            if (result.ok() && result.body.contains("config") && result.body["config"].is_object()) {
                applyConfig(id, result.body["config"], expected);
            } else {
                LogWarn << "query settings failed, config_id=" << id << logEnd;
            }
        });
}

bool SettingConfig::changeConfig(std::string_view method, const std::u8string& id, nlohmann::json body,
                                 nlohmann::json next, std::function<void(bool)> complete) {
    if (!ready_ || uid_.empty() || id.empty() || !LumaClient::instance().connectionId()) {
        return false;
    }
    configs_[id] = next;
    body["uid"] = to_json(uid_);
    body["instanceId"] = to_json(id);
    requests_.callAsync(u8"config:" + id, method, body,
        [this, id, expected = std::move(next)](CallResult result) {
            if (result.ok() && result.body.contains("config") && result.body["config"].is_object()) {
                applyConfig(id, result.body["config"], expected);
            } else if (LumaClient::instance().connectionId()) {
                refreshConfig(id);
            }
        },
        [complete = std::move(complete)](CallResult result) {
            if (complete) {
                complete(result.ok() && result.body.contains("config") && result.body["config"].is_object());
            }
        });
    return true;
}
bool SettingConfig::setConfig(const std::u8string& id, const nlohmann::json& value,
                              std::function<void(bool)> complete) {
    return value.is_object() && changeConfig("settings.replace", id, {{"config", value}}, value, std::move(complete));
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
    changeConfig("settings.patch", id, {{"patch", patch}}, std::move(next));
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
    auto next = favorites_;
    auto& list = next["pages"][to_char(std::u8string(page))];
    if (!list.is_array()) {
        list = nlohmann::json::array();
    }
    if (favorite) {
        list.push_back(id);
    } else {
        list.erase(std::remove(list.begin(), list.end(), nlohmann::json(id)), list.end());
    }
    return changeFavorites("settings.setFavorite",
                           {{"page", to_json(std::u8string(page))}, {"effectId", id}, {"favorite", favorite}},
                           std::move(next));
}

void SettingConfig::refreshFavorites() {
    requests_.callAsync(u8"favorites", "settings.getFavorites", {{"uid", to_json(uid_)}}, [this](CallResult result) {
        if (result.ok() && result.body.contains("favorites") && result.body["favorites"].is_object()) {
            const bool changed = favorites_ != result.body["favorites"];
            favorites_ = result.body["favorites"];
            if (changed) {
                emit favoritesChanged();
            }
        } else {
            LogWarn << "query favorites failed" << logEnd;
        }
    });
}

bool SettingConfig::changeFavorites(std::string_view method, nlohmann::json body, nlohmann::json next,
                                    std::function<void(bool)> complete) {
    if (!ready_ || uid_.empty() || !LumaClient::instance().connectionId()) {
        return false;
    }
    favorites_ = next;
    body["uid"] = to_json(uid_);
    requests_.callAsync(u8"favorites", method, body,
        [this, expected = std::move(next)](CallResult result) {
            if (result.ok() && result.body.contains("favorites") && result.body["favorites"].is_object()) {
                favorites_ = result.body["favorites"];
                if (favorites_ != expected) {
                    emit favoritesChanged();
                }
            } else if (LumaClient::instance().connectionId()) {
                refreshFavorites();
            }
        },
        [complete = std::move(complete)](CallResult result) {
            if (complete) {
                complete(result.ok() && result.body.contains("favorites") && result.body["favorites"].is_object());
            }
        });
    return true;
}
bool SettingConfig::replaceFavorites(const nlohmann::json& favorites, std::function<void(bool)> complete) {
    if (!favorites.is_object() || favorites.value("version", nlohmann::json()) != 1 ||
        !favorites.contains("pages") || !favorites["pages"].is_object()) {
        return false;
    }
    return changeFavorites("settings.replaceFavorites", {{"favorites", favorites}}, favorites, std::move(complete));
}
