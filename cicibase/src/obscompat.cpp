// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#include "obscompat.h"

#include <cassert>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <string>

#include "ciciconfigure.h"

namespace {

bool isDigit(char8_t ch) {
    return std::isdigit(static_cast<unsigned char>(ch)) != 0;
}

bool isSpace(char8_t ch) {
    return std::isspace(static_cast<unsigned char>(ch)) != 0;
}

void skipSpaces(std::u8string_view text, size_t& pos) {
    while (pos < text.size() && isSpace(text[pos])) {
        ++pos;
    }
}

bool parseNumber(std::u8string_view text, size_t& pos, uint32_t& value) {
    if (pos >= text.size() || !isDigit(text[pos])) {
        return false;
    }

    uint64_t parsed = 0;
    while (pos < text.size() && isDigit(text[pos])) {
        parsed = parsed * 10 + static_cast<uint32_t>(text[pos] - u8'0');
        if (parsed > std::numeric_limits<uint32_t>::max()) {
            return false;
        }
        ++pos;
    }

    value = static_cast<uint32_t>(parsed);
    return true;
}

bool consumeDot(std::u8string_view text, size_t& pos) {
    if (pos >= text.size() || text[pos] != u8'.') {
        return false;
    }

    ++pos;
    return true;
}

bool hasAllowedSuffix(std::u8string_view text, size_t pos) {
    skipSpaces(text, pos);
    if (pos == text.size()) {
        return true;
    }

    return text[pos] == u8'-' || text[pos] == u8'+';
}

}

namespace CiciOBSCompat {

bool parseVersion(std::u8string_view text, Version& version) {
    size_t pos = 0;
    skipSpaces(text, pos);

    Version parsed;
    if (!parseNumber(text, pos, parsed.major_)) {
        return false;
    }
    if (!consumeDot(text, pos) || !parseNumber(text, pos, parsed.minor_)) {
        return false;
    }
    if (!consumeDot(text, pos) || !parseNumber(text, pos, parsed.patch_)) {
        return false;
    }

    if (pos < text.size() && text[pos] == u8'.') {
        ++pos;
        uint32_t ignored = 0;
        if (!parseNumber(text, pos, ignored)) {
            return false;
        }
    }

    if (!hasAllowedSuffix(text, pos)) {
        return false;
    }

    version = parsed;
    return true;
}

int compareVersion(const Version& lhs, const Version& rhs) {
    if (lhs.major_ != rhs.major_) {
        return lhs.major_ < rhs.major_ ? -1 : 1;
    }
    if (lhs.minor_ != rhs.minor_) {
        return lhs.minor_ < rhs.minor_ ? -1 : 1;
    }
    if (lhs.patch_ != rhs.patch_) {
        return lhs.patch_ < rhs.patch_ ? -1 : 1;
    }

    return 0;
}

std::u8string versionToString(const Version& version) {
    return to_u8(version.major_) + u8"." + to_u8(version.minor_) + u8"." + to_u8(version.patch_);
}

Version minimumSupportedVersion() {
    Version version;
    if (!parseVersion(CICI_MIN_SUPPORTED_OBS_VERSION, version)) {
        assert(false && "invalid CICI_MIN_SUPPORTED_OBS_VERSION");
        std::abort();
    }

    return version;
}

std::u8string minimumSupportedVersionString() {
    return versionToString(minimumSupportedVersion());
}

bool isSupportedVersion(const Version& version) {
    return compareVersion(version, minimumSupportedVersion()) >= 0;
}

bool isSupportedVersion(std::u8string_view text) {
    Version version;
    return parseVersion(text, version) && isSupportedVersion(version);
}

}
