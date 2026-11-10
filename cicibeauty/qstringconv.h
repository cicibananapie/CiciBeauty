// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <QByteArray>
#include <QString>
#include <string>
#include <cstring>
#include <filesystem>
#include <ostream>

#include "ciciglobal_pch.h"
#include "nlohmann/json.hpp"

// Qt QString to/from UTF-8 conversion utilities
// This header is only used in cicibeauty module which depends on Qt

// Convert QString to std::u8string
CICI_FORCE_INLINE std::u8string to_u8(const QString& s) {
    auto utf8_bytes = s.toUtf8();
    return std::u8string(reinterpret_cast<const char8_t*>(utf8_bytes.constData()), 
                       utf8_bytes.size());
}

// Allow std::ostream-based logs to print QString directly.
CICI_FORCE_INLINE std::ostream& operator<<(std::ostream& os, const QString& s) {
    const QByteArray utf8_bytes = s.toUtf8();
    os.write(utf8_bytes.constData(), static_cast<std::streamsize>(utf8_bytes.size()));
    return os;
}

CICI_FORCE_INLINE nlohmann::json to_json(const QString &s) {
	nlohmann::json j;

    j = to_char(to_u8(s));

    return j;
}

// Convert std::u8string to QString
CICI_FORCE_INLINE QString from_u8(const std::u8string& s) {
    return QString::fromUtf8(s.data(), static_cast<int>(s.size()));
}

CICI_FORCE_INLINE QString from_json(const nlohmann::json& j) {
	if (!j.is_string()) {
		return QString();
    }

    return from_u8(to_u8(j));
}

CICI_FORCE_INLINE QString from_path(const std::filesystem::path& p) {
	return from_u8(p.u8string());
}
