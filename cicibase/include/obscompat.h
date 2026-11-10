// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#pragma once

#include <stdint.h>

#include <string>
#include <string_view>

namespace CiciOBSCompat {

struct Version {
    uint32_t major_ = 0;
    uint32_t minor_ = 0;
    uint32_t patch_ = 0;
};

bool parseVersion(std::u8string_view text, Version& version);
int compareVersion(const Version& lhs, const Version& rhs);
std::u8string versionToString(const Version& version);

Version minimumSupportedVersion();
std::u8string minimumSupportedVersionString();

bool isSupportedVersion(const Version& version);
bool isSupportedVersion(std::u8string_view text);

}
