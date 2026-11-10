// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#pragma once

#include "message.h"

namespace cici::protocol {

enum class CallStatus {
    Success,
    NotConnected,
    Timeout,
    Closed,
    RemoteError,
    InvalidRequest,
};

struct CallResult {
    CallStatus status = CallStatus::Success;
    nlohmann::json body = nlohmann::json::object();
    std::string errorCode;
    std::string errorMessage;

    bool ok() const {
        return status == CallStatus::Success;
    }
};

inline JsonMessage callRequest(uint64_t requestId, std::string_view method, const nlohmann::json& body) {
    JsonMessage message(method, body);
    message.value["requestId"] = requestId;
    return message;
}

inline JsonMessage callResponse(uint64_t requestId, const CallResult& result) {
    JsonMessage message;
    message.value = {{"version", 1}, {"requestId", requestId}, {"ok", result.ok()}};
    if (result.ok()) {
        message.value["body"] = result.body;
    } else {
        message.value["error"] = {
            {"code", result.errorCode},
            {"message", result.errorMessage},
        };
    }
    return message;
}

} // namespace cici::protocol
