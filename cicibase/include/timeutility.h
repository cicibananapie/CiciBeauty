// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#pragma once

#include <stdint.h>
#include <string>

class TimeUtility {
public:
	static std::u8string localNow();
    static int currentHour();
    static uint64_t utcMsNow();
    static void sleepFor(int ms);
};
