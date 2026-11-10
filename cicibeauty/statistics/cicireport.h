// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <string>

#include "nlohmann/json.hpp"

class CiciReport {
public:
    static void reportEvent(const std::u8string& event, const nlohmann::json& properties = nlohmann::json::object());
};
