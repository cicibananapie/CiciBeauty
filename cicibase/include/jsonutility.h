// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#pragma once

#include <cassert>
#include <cstdint>

#include <string>
#include <string_view>

#include "ciciglobal_pch.h"
#include "nlohmann/json.hpp"

namespace JsonUtility {

namespace detail {

CICI_FORCE_INLINE std::string_view jsonKeyView(std::u8string_view key) {
    return key.empty() ? std::string_view()
                       : std::string_view(to_char(key.data()), key.size());
}

CICI_FORCE_INLINE bool decodeJsonPointerToken(const std::string& token, std::string& decoded) {
    decoded.clear();
    for (size_t i = 0; i < token.size(); ++i) {
        if (token[i] != '~') {
            decoded.push_back(token[i]);
            continue;
        }
        if (i + 1 >= token.size()) {
            assert(false);
            return false;
        }

        const char next = token[++i];
        if (next == '0') {
            decoded.push_back('~');
        } else if (next == '1') {
            decoded.push_back('/');
        } else {
            assert(false);
            return false;
        }
    }

    return true;
}

CICI_FORCE_INLINE bool arrayIndexFromToken(const std::string& token, size_t size, size_t& index) {
    if (token.empty() || token == "-") {
        return false;
    }

    size_t value = 0;
    for (const char c : token) {
        if (c < '0' || c > '9') {
            assert(false);
            return false;
        }
        value = value * 10 + static_cast<size_t>(c - '0');
    }

    if (value >= size) {
        return false;
    }

    index = value;
    return true;
}

CICI_FORCE_INLINE bool jsonPointerExists(const nlohmann::json& j, const std::string& path) {
    if (path.empty()) {
        return true;
    }
    if (path[0] != '/') {
        assert(false);
        return false;
    }

    const nlohmann::json* current = &j;
    size_t start = 1;
    while (start <= path.size()) {
        const size_t end = path.find('/', start);
        std::string token;
        if (!decodeJsonPointerToken(path.substr(start, end == std::string::npos
                                                       ? std::string::npos
                                                       : end - start),
                                    token)) {
            return false;
        }

        if (current->is_object()) {
            auto it = current->find(token);
            if (it == current->end()) {
                return false;
            }
            current = &(*it);
        } else if (current->is_array()) {
            size_t index = 0;
            if (!arrayIndexFromToken(token, current->size(), index)) {
                return false;
            }
            current = &(*current)[index];
        } else {
            return false;
        }

        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    return true;
}

CICI_FORCE_INLINE void ensureJsonPointerParents(nlohmann::json& j, const std::string& path) {
    if (path.empty() || path[0] != '/') {
        assert(false);
        return;
    }

    nlohmann::json* current = &j;
    size_t start = 1;
    while (start <= path.size()) {
        const size_t end = path.find('/', start);
        if (end == std::string::npos) {
            break;
        }

        std::string token;
        if (!decodeJsonPointerToken(path.substr(start, end - start), token)) {
            return;
        }

        if (current->is_object()) {
            auto it = current->find(token);
            if (it == current->end()) {
                it = current->emplace(token, nlohmann::json::object()).first;
            }
            if (!it->is_object() && !it->is_array()) {
                return;
            }
            current = &(*it);
        } else if (current->is_array()) {
            size_t index = 0;
            if (!arrayIndexFromToken(token, current->size(), index)) {
                return;
            }
            current = &(*current)[index];
        } else {
            return;
        }

        start = end + 1;
    }
}

}  // namespace detail

CICI_FORCE_INLINE const nlohmann::json& emptyObject() {
    static const auto object = nlohmann::json::object();
    return object;
}

CICI_FORCE_INLINE const nlohmann::json& emptyArray() {
    static const auto array = nlohmann::json::array();
    return array;
}

CICI_FORCE_INLINE const nlohmann::json& objectAt(const nlohmann::json& j,
                                                 std::u8string_view key) {
    auto it = j.find(detail::jsonKeyView(key));
    return it != j.end() && it->is_object() ? *it : emptyObject();
}

CICI_FORCE_INLINE const nlohmann::json& objectAtIndex(const nlohmann::json& j,
                                                      uint32_t index) {
    return j.is_array() && index < j.size() && j[index].is_object()
        ? j[index]
        : emptyObject();
}

CICI_FORCE_INLINE const nlohmann::json& arrayAt(const nlohmann::json& j,
                                                std::u8string_view key) {
    auto it = j.find(detail::jsonKeyView(key));
    return it != j.end() && it->is_array() ? *it : emptyArray();
}

CICI_FORCE_INLINE bool boolAt(const nlohmann::json& j,
                              std::u8string_view key,
                              bool fallback = false) {
    auto it = j.find(detail::jsonKeyView(key));
    return it != j.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

CICI_FORCE_INLINE uint32_t uintAt(const nlohmann::json& j,
                                  std::u8string_view key,
                                  uint32_t fallback = 0) {
    auto it = j.find(detail::jsonKeyView(key));
    if (it == j.end()) {
        return fallback;
    }
    if (it->is_number_unsigned()) {
        return it->get<uint32_t>();
    }
    if (it->is_number_integer()) {
        const auto value = it->get<int64_t>();
        return value >= 0 ? static_cast<uint32_t>(value) : fallback;
    }

    return fallback;
}

CICI_FORCE_INLINE int intAt(const nlohmann::json& j,
                            std::u8string_view key,
                            int fallback = 0) {
    auto it = j.find(detail::jsonKeyView(key));
    if (it == j.end()) {
        return fallback;
    }
    if (it->is_number_integer()) {
        return it->get<int>();
    }
    if (it->is_number_unsigned()) {
        return static_cast<int>(it->get<uint32_t>());
    }

    return fallback;
}

CICI_FORCE_INLINE float floatAt(const nlohmann::json& j,
                                std::u8string_view key,
                                float fallback = 0.0f) {
    auto it = j.find(detail::jsonKeyView(key));
    return it != j.end() && it->is_number() ? it->get<float>() : fallback;
}

CICI_FORCE_INLINE std::u8string u8StringAt(const nlohmann::json& j,
                                           std::u8string_view key) {
    auto it = j.find(detail::jsonKeyView(key));
    return it != j.end() && it->is_string() ? to_u8(*it) : std::u8string();
}

CICI_FORCE_INLINE bool uiValueAt(const nlohmann::json& j,
                                 std::u8string_view key,
                                 uint32_t& value) {
    auto it = j.find(detail::jsonKeyView(key));
    if (it == j.end()) {
        return false;
    }
    if (it->is_number_unsigned()) {
        value = it->get<uint32_t>();
        return true;
    }
    if (it->is_number_integer()) {
        const auto n = it->get<int64_t>();
        if (n >= 0) {
            value = static_cast<uint32_t>(n);
            return true;
        }
    }

    assert(false);
    return false;
}

namespace Patch {

CICI_FORCE_INLINE bool apply(nlohmann::json& config, const nlohmann::json& patch) {
    if (!config.is_object() || !patch.is_array()) {
        return false;
    }

    auto nextConfig = config;
    auto effectivePatch = nlohmann::json::array();
    for (const auto& op : patch) {
        if (!op.is_object()) {
            assert(false);
            continue;
        }

        const auto opName = op.find("op");
        const auto path = op.find("path");
        if (opName == op.end() || !opName->is_string() ||
            path == op.end() || !path->is_string()) {
            assert(false);
            continue;
        }

        if (*opName == "remove" && !detail::jsonPointerExists(nextConfig, path->get<std::string>())) {
            continue;
        }
        if (*opName == "add") {
            detail::ensureJsonPointerParents(nextConfig, path->get<std::string>());
        }
        effectivePatch.push_back(op);
    }
    if (effectivePatch.empty()) {
        return true;
    }

    config = nextConfig.patch(effectivePatch);
    return true;
}

CICI_FORCE_INLINE void appendAdd(nlohmann::json& patch,
                                 std::u8string_view path,
                                 const nlohmann::json& value) {
    const std::u8string pathU8(path);
    patch.push_back({
        {to_char(u8"op"), to_char(u8"add")},
        {to_char(u8"path"), to_char(pathU8)},
        {to_char(u8"value"), value},
    });
}

CICI_FORCE_INLINE nlohmann::json add(std::u8string_view path,
                                     const nlohmann::json& value) {
    auto patch = nlohmann::json::array();
    appendAdd(patch, path, value);
    return patch;
}

CICI_FORCE_INLINE void appendRemove(nlohmann::json& patch,
                                    std::u8string_view path) {
    const std::u8string pathU8(path);
    patch.push_back({
        {to_char(u8"op"), to_char(u8"remove")},
        {to_char(u8"path"), to_char(pathU8)},
    });
}

CICI_FORCE_INLINE nlohmann::json remove(std::u8string_view path) {
    auto patch = nlohmann::json::array();
    appendRemove(patch, path);
    return patch;
}

}  // namespace Patch

}  // namespace JsonUtility
