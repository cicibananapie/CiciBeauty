// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "protocol/message.h"

enum class LoginReadySource {
    LocalCache,
    Server
};
enum class LoginFailureReason {
    None,
    Other,
    Unauthorized,
    Forbidden,
    ServiceUnavailable
};

// Shared state names preserve the existing LoginLogic API on both sides.
struct LoginStates {
    enum State {
        InitialState,
        Logining,
        LocalLogined,
        Logined,
        LoginFailed
    };
};

struct LoginInfo {
    LoginStates::State state = LoginStates::InitialState;
    std::u8string uid;
    std::u8string email;
    std::u8string nickname;
    std::u8string avatarUrl;

    bool hasUsableSession() const {
        return state == LoginStates::LocalLogined || state == LoginStates::Logined;
    }
};

enum SubscriptionType : unsigned int {
    NotSubscribed,
    Trial,
    Temporary,
    Monthly,
    OneMonth,
    Yearly,
    OneYear,
    Expired
};

struct SubscriptionInfo {
    SubscriptionType type_ = NotSubscribed;
    std::chrono::system_clock::time_point expireTime_;
    std::chrono::system_clock::time_point serverTime_;
};

struct SubscriptionStatus {
    std::u8string uid;
    bool isVip = false;
    uint32_t daysBeforeExpire = 0;
    SubscriptionType type = NotSubscribed;
    SubscriptionInfo info;
};

namespace cici::protocol {

inline nlohmann::json loginInfoJson(const LoginInfo& info) {
    return {{"state", static_cast<unsigned>(info.state)},
            {"uid", to_json(info.uid)},
            {"email", to_json(info.email)},
            {"nickname", to_json(info.nickname)},
            {"avatarUrl", to_json(info.avatarUrl)}};
}

inline bool readLoginInfo(const nlohmann::json& body, LoginInfo& info) {
    if (!body.is_object() || !body.contains("state") || !body["state"].is_number_unsigned() ||
        body["state"].get<uint64_t>() > LoginStates::LoginFailed) {
        return false;
    }
    for (const char* key : {"uid", "email", "nickname", "avatarUrl"}) {
        if (!body.contains(key) || !body[key].is_string()) {
            return false;
        }
    }
    info = {static_cast<LoginStates::State>(body["state"].get<unsigned>()), to_u8(body["uid"].get<std::string>()),
            to_u8(body["email"].get<std::string>()), to_u8(body["nickname"].get<std::string>()),
            to_u8(body["avatarUrl"].get<std::string>())};
    return true;
}

inline int64_t subscriptionTimeMs(std::chrono::system_clock::time_point time) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
}

inline nlohmann::json subscriptionStatusJson(const SubscriptionStatus& status) {
    return {{"uid", to_json(status.uid)},
            {"isVip", status.isVip},
            {"daysBeforeExpire", status.daysBeforeExpire},
            {"type", static_cast<unsigned>(status.type)},
            {"infoType", static_cast<unsigned>(status.info.type_)},
            {"expireAtMs", subscriptionTimeMs(status.info.expireTime_)},
            {"serverTimeMs", subscriptionTimeMs(status.info.serverTime_)}};
}

inline bool readSubscriptionStatus(const nlohmann::json& body, SubscriptionStatus& status) {
    if (!body.is_object() || !body.contains("uid") || !body["uid"].is_string() || !body.contains("isVip") ||
        !body["isVip"].is_boolean() || !body.contains("daysBeforeExpire") ||
        !body["daysBeforeExpire"].is_number_unsigned() || body["daysBeforeExpire"].get<uint64_t>() > UINT32_MAX) {
        return false;
    }
    for (const char* key : {"type", "infoType"}) {
        if (!body.contains(key) || !body[key].is_number_unsigned() || body[key].get<uint64_t>() > Expired) {
            return false;
        }
    }
    for (const char* key : {"expireAtMs", "serverTimeMs"}) {
        if (!body.contains(key) || !body[key].is_number_integer() ||
            (body[key].is_number_unsigned() && body[key].get<uint64_t>() > INT64_MAX)) {
            return false;
        }
        const auto milliseconds = body[key].get<int64_t>();
        const auto maximum =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::duration::max()).count();
        if (milliseconds < 0 || milliseconds > maximum) {
            return false;
        }
    }
    status = {to_u8(body["uid"].get<std::string>()),
              body["isVip"].get<bool>(),
              body["daysBeforeExpire"].get<uint32_t>(),
              static_cast<SubscriptionType>(body["type"].get<unsigned>()),
              {static_cast<SubscriptionType>(body["infoType"].get<unsigned>()),
               std::chrono::system_clock::time_point(std::chrono::milliseconds(body["expireAtMs"].get<int64_t>())),
               std::chrono::system_clock::time_point(std::chrono::milliseconds(body["serverTimeMs"].get<int64_t>()))}};
    return true;
}

} // namespace cici::protocol
