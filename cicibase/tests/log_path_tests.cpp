// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "log.h"

namespace {

std::filesystem::path cleanupRoot;

void cleanup() {
    std::error_code ec;
    std::filesystem::remove_all(cleanupRoot, ec);
}

}  // namespace

int main() {
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    cleanupRoot = std::filesystem::temp_directory_path() /
                  "cici-log-path-tests" / unique;
    if (std::atexit(cleanup) != 0) {
        std::cerr << "failed to register log path test cleanup\n";
        return 1;
    }

    const std::filesystem::path logFile = cleanupRoot /
        std::filesystem::path(u8"\u4e2d\u6587\u7528\u6237") / u8"cicibeautystub.log";

    try {
        initCiciLogConfig(0, logFile.u8string());
        LogInfo << "UTF-8 log path probe" << logEnd;
        LogImpl::instance().flush();
    } catch (const std::exception& error) {
        std::cerr << "log initialization threw: " << error.what() << '\n';
        return 1;
    }

    if (!std::filesystem::is_regular_file(logFile)) {
        std::cerr << "log file was not created at the requested UTF-8 path\n";
        return 1;
    }

    return 0;
}
