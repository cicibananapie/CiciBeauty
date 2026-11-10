// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#include "cicilocale.h"

#include <algorithm>
#include <string>

namespace {

std::string normalizedTag(std::u8string_view locale) {
    std::string tag;
    tag.reserve(locale.size());

    for (char8_t c8 : locale) {
        char c = static_cast<char>(c8);
        if (c == '.') {
            break;
        }

        if (c == '_') {
            c = '-';
        } else if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }

        tag.push_back(c);
    }

    return tag;
}

bool containsToken(const std::string& text, const char* token) {
    return text.find(token) != std::string::npos;
}

}

namespace CiciLocale {

const char8_t kEnglishUS[] = u8"en_US";
const char8_t kSimplifiedChinese[] = u8"zh_CN";
const char8_t kTraditionalChinese[] = u8"zh_TW";

std::u8string normalize(std::u8string_view locale) {
    const std::string tag = normalizedTag(locale);
    if (tag.empty()) {
        return kEnglishUS;
    }

    if (tag == "zh" || tag.starts_with("zh-")) {
        if (containsToken(tag, "hant") ||
            containsToken(tag, "-tw") ||
            containsToken(tag, "-hk") ||
            containsToken(tag, "-mo")) {
            return kTraditionalChinese;
        }

        return kSimplifiedChinese;
    }

    if (tag == "en" || tag.starts_with("en-")) {
        return kEnglishUS;
    }

    return kEnglishUS;
}

}
