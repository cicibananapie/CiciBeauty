// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "log.h"
#include "misc.h"

inline std::u8string trimAsciiWhitespace(std::u8string value) {
    constexpr std::u8string_view kAsciiWhitespace = u8" \t\r\n";
    size_t begin = value.find_first_not_of(kAsciiWhitespace);
    if (begin == std::u8string::npos) {
        return {};
    }

    size_t end = value.find_last_not_of(kAsciiWhitespace);
    return value.substr(begin, end - begin + 1);
}

inline bool readTrimmedTextFile(const std::filesystem::path& file, std::u8string& content) {
    content.clear();
    if (!FileSystemOp::readFile(file, content)) {
        return false;
    }

    content = trimAsciiWhitespace(std::move(content));
    return true;
}

inline bool directoryHasEntries(const std::filesystem::path& directory) {
    std::error_code ec;
    std::filesystem::directory_iterator it(directory, ec);
    if (ec) {
        LogError << "directory_iterator failed: " << directory.u8string() << " error:" << ec.message() << logEnd;
        return false;
    }

    return it != std::filesystem::directory_iterator();
}

inline std::filesystem::path installSlotEntryModulePath(const std::filesystem::path& slotPath) {
#if CICI_OS_MACOS
    return slotPath / u8"Contents" / u8"Frameworks" / u8"cicibeauty.so";
#elif CICI_OS_WINDOWS
    return slotPath / u8"cicibeauty.dll";
#else
#error "Unsupported platform"
#endif
}

inline std::filesystem::path installSlotEntryModulePathForName(const std::filesystem::path& installRoot,
                                                               std::u8string_view slotName) {
    return installSlotEntryModulePath(installRoot / std::u8string(slotName));
}

inline bool isInstallSlotDirectory(const std::filesystem::path& slotPath) {
    if (!FileSystemOp::isFolderExists(slotPath)) {
        return false;
    }

    return FileSystemOp::isFileExists(installSlotEntryModulePath(slotPath));
}

inline bool slotContainsInstallEntryModule(const std::filesystem::path& installRoot, std::u8string_view slotName) {
    return FileSystemOp::isFileExists(installSlotEntryModulePathForName(installRoot, slotName));
}

inline bool readActiveInstallSlotName(const std::filesystem::path& installRoot, std::u8string& slotName,
                                      std::u8string_view configFileName = u8"config") {
    if (!readTrimmedTextFile(installRoot / std::u8string(configFileName), slotName)) {
        return false;
    }

    return !slotName.empty();
}

inline bool enumerateInstallSlots(const std::filesystem::path& installRoot, std::vector<std::u8string>& slots) {
    slots.clear();

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(installRoot, ec)) {
        if (ec) {
            LogError << "directory iteration failed: " << installRoot.u8string() << " error:" << ec.message()
                     << logEnd;
            return false;
        }

        if (!entry.is_directory(ec)) {
            if (ec) {
                LogError << "check directory type failed: " << entry.path().u8string()
                         << " error:" << ec.message() << logEnd;
                return false;
            }
            continue;
        }

        auto slotName = entry.path().filename().u8string();
        if (slotContainsInstallEntryModule(installRoot, slotName)) {
            slots.push_back(slotName);
        }
    }

    return true;
}

inline bool isManagedInstallRoot(const std::filesystem::path& installRoot,
                                 std::u8string_view activeConfigFileName = u8"config") {
    if (!FileSystemOp::isFolderExists(installRoot)) {
        return false;
    }

    std::u8string activeSlot;
    if (readActiveInstallSlotName(installRoot, activeSlot, activeConfigFileName) &&
        slotContainsInstallEntryModule(installRoot, activeSlot)) {
        return true;
    }

    std::vector<std::u8string> slots;
    return enumerateInstallSlots(installRoot, slots) && !slots.empty();
}
