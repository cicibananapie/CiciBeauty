// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#pragma once

#include <assert.h>

#include <charconv>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

struct VersionSlotInfo {
    std::u8string baseName_;
    int major_ = 0;
    int minor_ = 0;
    int patch_ = 0;
    int suffix_ = 0;
    bool hasSuffix_ = false;
};

namespace VersionSlotDetail {

struct SplitNameResult {
    std::u8string semanticPart_;
    std::u8string suffixPart_;
    bool hasAppSuffix_ = false;
    bool hasNumericSuffix_ = false;
};

inline SplitNameResult splitName(std::u8string_view slotName) {
    SplitNameResult result;

    std::u8string core(slotName);
    if (core.ends_with(u8".app")) {
        result.hasAppSuffix_ = true;
        core.erase(core.size() - 4);
    }

    size_t pos = core.find(u8'_');
    if (pos == std::u8string::npos) {
        result.semanticPart_ = std::move(core);
        return result;
    }

    result.hasNumericSuffix_ = true;
    result.semanticPart_ = core.substr(0, pos);
    result.suffixPart_ = core.substr(pos + 1);
    return result;
}

inline bool parseNonNegativeInt(std::u8string_view text, int& value) {
    if (text.empty()) {
        return false;
    }

    int parsed = 0;
    const char* begin = reinterpret_cast<const char*>(text.data());
    const char* end = begin + text.size();
    auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc() || result.ptr != end || parsed < 0) {
        return false;
    }

    value = parsed;
    return true;
}

inline std::u8string buildBaseName(const SplitNameResult& split) {
    std::u8string baseName = split.semanticPart_;
    if (split.hasAppSuffix_) {
        baseName += u8".app";
    }
    return baseName;
}

inline int lexicalCompare(std::u8string_view lhs, std::u8string_view rhs) {
    int cmp = lhs.compare(rhs);
    if (cmp < 0) {
        return -1;
    }
    if (cmp > 0) {
        return 1;
    }
    return 0;
}

}  // namespace VersionSlotDetail

inline bool parseVersionSlotName(std::u8string_view slotName, VersionSlotInfo& info) {
    using namespace VersionSlotDetail;

    SplitNameResult split = splitName(slotName);
    if (split.semanticPart_.empty()) {
        return false;
    }

    size_t dot1 = split.semanticPart_.find(u8'.');
    if (dot1 == std::u8string::npos) {
        return false;
    }

    size_t dot2 = split.semanticPart_.find(u8'.', dot1 + 1);
    if (dot2 == std::u8string::npos) {
        return false;
    }

    if (split.semanticPart_.find(u8'.', dot2 + 1) != std::u8string::npos) {
        return false;
    }

    int major = 0;
    int minor = 0;
    int patch = 0;
    if (!parseNonNegativeInt(std::u8string_view(split.semanticPart_).substr(0, dot1), major) ||
        !parseNonNegativeInt(std::u8string_view(split.semanticPart_).substr(dot1 + 1, dot2 - dot1 - 1), minor) ||
        !parseNonNegativeInt(std::u8string_view(split.semanticPart_).substr(dot2 + 1), patch)) {
        return false;
    }

    int suffix = 0;
    if (split.hasNumericSuffix_) {
        if (!parseNonNegativeInt(split.suffixPart_, suffix) || suffix <= 0) {
            return false;
        }
    }

    info.baseName_ = buildBaseName(split);
    info.major_ = major;
    info.minor_ = minor;
    info.patch_ = patch;
    info.suffix_ = suffix;
    info.hasSuffix_ = split.hasNumericSuffix_;
    return true;
}

inline bool isVersionSlotName(std::u8string_view slotName) {
    VersionSlotInfo info;
    return parseVersionSlotName(slotName, info);
}

inline int compareVersionSlotNames(std::u8string_view lhs, std::u8string_view rhs) {
    VersionSlotInfo lhsInfo;
    VersionSlotInfo rhsInfo;
    bool lhsOk = parseVersionSlotName(lhs, lhsInfo);
    bool rhsOk = parseVersionSlotName(rhs, rhsInfo);

    if (lhsOk && rhsOk) {
        if (lhsInfo.major_ != rhsInfo.major_) {
            return lhsInfo.major_ > rhsInfo.major_ ? 1 : -1;
        }
        if (lhsInfo.minor_ != rhsInfo.minor_) {
            return lhsInfo.minor_ > rhsInfo.minor_ ? 1 : -1;
        }
        if (lhsInfo.patch_ != rhsInfo.patch_) {
            return lhsInfo.patch_ > rhsInfo.patch_ ? 1 : -1;
        }
        if (lhsInfo.suffix_ != rhsInfo.suffix_) {
            return lhsInfo.suffix_ > rhsInfo.suffix_ ? 1 : -1;
        }
        return VersionSlotDetail::lexicalCompare(lhs, rhs);
    }

    if (lhsOk != rhsOk) {
        return lhsOk ? 1 : -1;
    }

    return VersionSlotDetail::lexicalCompare(lhs, rhs);
}

inline bool versionSlotGreater(std::u8string_view lhs, std::u8string_view rhs) {
    return compareVersionSlotNames(lhs, rhs) > 0;
}

inline std::u8string versionSlotBaseName(std::u8string_view slotName) {
    VersionSlotInfo info;
    if (parseVersionSlotName(slotName, info)) {
        return info.baseName_;
    }

    return std::u8string(slotName);
}

inline std::u8string appendVersionSlotSuffix(std::u8string_view baseSlotName, int suffix) {
    assert(suffix > 0);
    if (suffix <= 0) {
        return versionSlotBaseName(baseSlotName);
    }

    std::u8string baseName = versionSlotBaseName(baseSlotName);
    std::filesystem::path path(baseName);
    std::u8string suffixText = to_u8(std::to_string(suffix));
    if (path.extension().u8string() == u8".app") {
        return path.stem().u8string() + u8"_" + suffixText + path.extension().u8string();
    }

    return baseName + u8"_" + suffixText;
}

inline std::u8string chooseUniqueVersionSlotName(const std::filesystem::path& installRoot,
                                                 std::u8string_view preferredSlotName) {
    std::u8string baseName = versionSlotBaseName(preferredSlotName);
    std::u8string candidate = baseName;

    for (int suffix = 2;; ++suffix) {
        std::error_code ec;
        bool exists = std::filesystem::exists(installRoot / candidate, ec);
        if (!ec && !exists) {
            return candidate;
        }
        if (ec) {
            return candidate;
        }

        candidate = appendVersionSlotSuffix(baseName, suffix);
    }
}
