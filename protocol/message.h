// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#pragma once

#include <string_view>
#include <utility>
#include <cstring>

#include "ciciglobal_pch.h"
#include "binarymsg.h"

namespace cici::protocol {

inline constexpr uint32_t kJsonMessageId = 1400;

struct JsonMessage : IPCMessage {
    nlohmann::json value;

    JsonMessage() = default;
    JsonMessage(std::string_view method, nlohmann::json body)
        : value({{"version", 1}, {"method", method}, {"body", std::move(body)}}) {
    }

    bool isRequest() const {
        return value.contains("requestId") && value.contains("method");
    }
    bool isResponse() const {
        return value.contains("requestId") && value.contains("ok");
    }

    bool fromBinary(const BinaryStream& data) {
        uint32_t header[3]; // Frame length, message ID, JSON string length.
        if (data.size() < sizeof(header)) {
            return false;
        }
        std::memcpy(header, data.data(), sizeof(header));
        return header[0] == data.size() && header[1] == kJsonMessageId && header[2] == data.size() - sizeof(header) &&
               IPCMessage::fromBinary(data);
    }

protected:
    uint32_t id() const override {
        return kJsonMessageId;
    }

    uint32_t pack(BinaryPack& bin) const override {
        return bin.push(BinaryStream(value.dump()));
    }

    bool unpack(BinaryUnpack& bin) override {
        BinaryStream text;
        if (!bin.pop(text)) {
            return false;
        }
        value = nlohmann::json::parse(text, nullptr, false);
        if (!value.is_object() || !value.contains("version") || !value["version"].is_number_integer() ||
            value["version"] != 1) {
            return false;
        }
        if (value.contains("requestId") &&
            (!value["requestId"].is_number_unsigned() || value["requestId"].get<uint64_t>() == 0)) {
            return false;
        }
        if (value.contains("ok")) {
            if (!value.contains("requestId") || value.contains("method") || !value["ok"].is_boolean()) {
                return false;
            }
            if (value["ok"].get<bool>()) {
                return value.contains("body") && value["body"].is_object() && !value.contains("error");
            }
            return !value.contains("body") && value.contains("error") && value["error"].is_object() &&
                   value["error"].contains("code") && value["error"]["code"].is_string() &&
                   !value["error"]["code"].get_ref<const std::string&>().empty() &&
                   value["error"].contains("message") && value["error"]["message"].is_string();
        }
        return !value.contains("error") && value.contains("method") && value["method"].is_string() &&
               !value["method"].get_ref<const std::string&>().empty() && value.contains("body") &&
               value["body"].is_object();
    }
};

} // namespace cici::protocol
