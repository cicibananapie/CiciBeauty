// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#pragma once
#include <string>
#include <string_view>
#include <functional>
#include <filesystem>
#include <vector>

#include "nlohmann/json_fwd.hpp"

class UrlOp {
public:
    static bool isUrl(std::u8string_view url);
};

class FileSystemOp {
public:
    static const std::filesystem::path& binPath();
    static const std::filesystem::path& resourcesPath();
    static const std::filesystem::path& bundlePath(); //should only be use when it's a real bundle.
    static const std::filesystem::path& roamingAppDataPath();
    static const std::filesystem::path& logPath();
    static const std::filesystem::path& cachePath();
    static const std::filesystem::path& localCachePath();
    static const std::filesystem::path& remoteCachePath();
    static bool isFileExists(const std::filesystem::path& path);
    static bool isFolderExists(const std::filesystem::path& path);
    
    static bool loadJson(const std::filesystem::path& path, nlohmann::json& j);
    static bool storeJson(const std::filesystem::path& path, const nlohmann::json& j);
    
    static bool copyFile(const std::filesystem::path& src, const std::filesystem::path& dst);

    typedef std::function<void(const std::u8string& relativePath, bool isDir)> CopyDirProgressFuncU8;
    static bool copyDirectory(const std::filesystem::path& src, const std::filesystem::path& dst,
                              CopyDirProgressFuncU8 progress = nullptr);

    typedef std::function<void(const std::u8string &relativePath)> VisitorFuncU8;
    static bool traverseDirectory(const std::filesystem::path &src, VisitorFuncU8 visitor);
    
    typedef std::function<void(const std::u8string& relativePath, bool isDir)> RemoveDirProgressFuncU8;
    static bool removeDir(const std::filesystem::path& path, RemoveDirProgressFuncU8 progress = nullptr);
    
    static bool renameDir(const std::filesystem::path& oldName, const std::filesystem::path& newName);
    
    static bool makeFileExecutable(const std::filesystem::path& file);

    static bool writeFile(const std::filesystem::path &file, uint8_t *data, uint32_t sz);

    static bool replaceFileAtomically(const std::filesystem::path& from,
                                      const std::filesystem::path& to);
    
    // UTF-8 versions using std::u8string
    static bool readFile(const std::filesystem::path& file, std::u8string& content);
    static bool writeFile(const std::filesystem::path &file, const std::u8string &content);
    static bool appendFileAtomically(const std::filesystem::path& file, const std::u8string& content);
    
    static bool createDirectories(const std::filesystem::path &p);
};

class ProcessThreadOp {
public:
    static uint32_t pid();
    static uint64_t tid();
    // Returns zero on success or the platform error code; closes/reaps the child automatically.
    static int launchDetached(const std::filesystem::path& executable, const std::vector<std::u8string>& arguments,
                              uint32_t& childPid, const std::filesystem::path& workingDirectory = {});
    // Blocks until the child exits and is reaped. Returns a launch/wait error, not the child's exit code.
    static int launchAndWait(const std::filesystem::path& executable, const std::vector<std::u8string>& arguments,
                             uint32_t& childPid, const std::filesystem::path& workingDirectory = {});
};

// UTF-8 <-> Wide conversions for Windows API interop.
// On macOS these APIs are explicitly disabled to prevent accidental misuse.
#if CICI_OS_WINDOWS
std::wstring utf8ToWide(const std::u8string &s);
inline std::wstring utf8ToWide(const char8_t *s) {
	if (!s) {
		return L"";
	}
	return utf8ToWide(std::u8string(s));
}
std::u8string wideToUtf8(const std::wstring &ws);
#elif CICI_OS_MACOS
std::wstring utf8ToWide(const std::u8string &s) = delete;
inline std::wstring utf8ToWide(const char8_t *s) = delete;
std::u8string wideToUtf8(const std::wstring &ws) = delete;
#else
#error "Unsupported platform"
#endif
