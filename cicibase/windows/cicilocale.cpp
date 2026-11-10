// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#include "cicilocale.h"

#include <Windows.h>

#include "misc.h"

namespace CiciLocale {

std::u8string systemLocale() {
    wchar_t localeName[LOCALE_NAME_MAX_LENGTH] = {};
    if (GetUserDefaultLocaleName(localeName, LOCALE_NAME_MAX_LENGTH) <= 0) {
        return kEnglishUS;
    }

    return normalize(wideToUtf8(localeName));
}

}
