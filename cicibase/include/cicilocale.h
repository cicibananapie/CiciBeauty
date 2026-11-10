// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#pragma once

#include <string>
#include <string_view>

namespace CiciLocale {

extern const char8_t kEnglishUS[];
extern const char8_t kSimplifiedChinese[];
extern const char8_t kTraditionalChinese[];

std::u8string normalize(std::u8string_view locale);
std::u8string systemLocale();

}
