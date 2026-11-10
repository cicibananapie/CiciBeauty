// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#include <assert.h>
#include <ctime>
#include <thread>
#include <chrono>

#include "timeutility.h"

void localTime(std::tm &localTm) {
    std::time_t now = std::time(nullptr);
#if CICI_OS_MACOS
    localtime_r(&now, &localTm);
#elif CICI_OS_WINDOWS
    localtime_s(&localTm, &now);
#else
#error "Unsupported platform"
#endif
}

std::u8string TimeUtility::localNow() {
    std::tm localTm;
    localTime(localTm);
    char buffer[26] = {};
    const size_t length = std::strftime(buffer, sizeof(buffer), "%a %b %d %H:%M:%S %Y", &localTm);
    assert(length > 0);
    if (length == 0) {
        return {};
    }

    return to_u8(buffer);
}

int TimeUtility::currentHour() {
    std::tm localTm;
    localTime(localTm);
    
    return localTm.tm_hour;
}
uint64_t TimeUtility::utcMsNow() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()
    ).count();
}

void TimeUtility::sleepFor(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
