// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

#include "cicibase.h"
#include "misc.h"
#include "uuid.h"
#include "nlohmann/json.hpp"

#if CICI_OS_MACOS
#include <cerrno>
#include <signal.h>
#elif CICI_OS_WINDOWS
#include <Windows.h>
#include <shellapi.h>
#endif

int main(int argc, char** argv) {
    std::vector<std::u8string> commandLine;
#if CICI_OS_WINDOWS
    // main() uses the system ANSI code page; inspect the native UTF-16 command
    // line so this test also works on machines whose code page is not UTF-8.
    int wideCount = 0;
    auto** wideArguments = CommandLineToArgvW(GetCommandLineW(), &wideCount);
    if (!wideArguments) {
        return 1;
    }
    for (int i = 0; i < wideCount; ++i) {
        commandLine.push_back(wideToUtf8(wideArguments[i]));
    }
    LocalFree(wideArguments);
#else
    for (int i = 0; i < argc; ++i) {
        commandLine.push_back(to_u8(argv[i]));
    }
#endif
    if (commandLine.size() >= 3 && (commandLine[1] == u8"--record" || commandLine[1] == u8"--delayed-record")) {
        const bool delayed = commandLine[1] == u8"--delayed-record";
        if (delayed) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        nlohmann::json args = nlohmann::json::array();
        for (size_t i = 3; i < commandLine.size(); ++i) {
            args.push_back(to_json(commandLine[i]));
        }
        std::ofstream out{std::filesystem::path(commandLine[2])};
        out << nlohmann::json{{"arguments", args}, {"cwd", to_json(std::filesystem::current_path().u8string())}};
        return out ? (delayed ? 7 : 0) : 1;
    }
    const auto temporary = std::filesystem::temp_directory_path() / (u8"cici process test " + CiciUUID::generate());
    std::filesystem::create_directories(temporary);
    const auto output = temporary / "arguments.json";
    uint32_t pid = 0;
    const std::vector<std::u8string> expected = {
        u8"",         u8"with spaces", u8"with\ttab",          u8"quoted \"argument\"",
        u8"\u4e2d\u6587\u53c2\u6570", u8"trailing\\",  u8"backslash\\\"quote", u8"two trailing\\\\"};
    std::vector<std::u8string> arguments{u8"--record", output.u8string()};
    arguments.insert(arguments.end(), expected.begin(), expected.end());
    const auto source = std::filesystem::absolute(std::filesystem::path(commandLine.front()));
    const auto executable =
        temporary / (std::filesystem::path(u8"helper with spaces \u4e2d\u6587").native() + source.extension().native());
    std::filesystem::copy_file(source, executable);
    const int error = ProcessThreadOp::launchDetached(executable, arguments, pid, temporary);
    bool passed = error == 0 && pid != 0;
    nlohmann::json received;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (passed && std::chrono::steady_clock::now() < deadline) {
        std::ifstream input(output);
        if (input) {
            received = nlohmann::json::parse(input, nullptr, false);
            if (received.is_object()) {
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    nlohmann::json expectedJson = nlohmann::json::array();
    for (const auto& value : expected) {
        expectedJson.push_back(to_json(value));
    }
    passed = passed && received.is_object() && received["arguments"] == expectedJson &&
             std::filesystem::equivalent(std::filesystem::path(to_u8(received.value("cwd", ""))), temporary);
#if CICI_OS_MACOS
    while (passed && std::chrono::steady_clock::now() < deadline && kill(pid, 0) == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    passed = passed && kill(pid, 0) == -1 && errno == ESRCH;
#endif
    const int missing = ProcessThreadOp::launchDetached(temporary / "missing executable", {}, pid);
    passed = passed && missing != 0 && pid == 0;
    std::filesystem::remove(output);
    arguments[0] = u8"--delayed-record";
    const int waited = ProcessThreadOp::launchAndWait(executable, arguments, pid, temporary);
    received = nlohmann::json();
    {
        std::ifstream input(output);
        if (input) {
            received = nlohmann::json::parse(input, nullptr, false);
        }
    }
    passed = passed && waited == 0 && pid != 0 && received.is_object() && received["arguments"] == expectedJson &&
        std::filesystem::equivalent(std::filesystem::path(to_u8(received.value("cwd", ""))), temporary);
#if CICI_OS_MACOS
    passed = passed && kill(pid, 0) == -1 && errno == ESRCH;
#endif
    const int missingWaited = ProcessThreadOp::launchAndWait(temporary / "missing executable", {}, pid);
    passed = passed && missingWaited != 0 && pid == 0;
    std::filesystem::remove(output);
    std::filesystem::remove(executable);
    std::filesystem::remove(temporary);
    std::cout << (passed ? "PASS" : "FAIL")
              << ": detached/waited launch, UTF-8/empty/quoted arguments, working directory, child cleanup and errors\n";
    return passed ? 0 : 1;
}
