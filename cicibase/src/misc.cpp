// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#include <assert.h>
#include <string>
#include <string_view>
#include <cmath>
#include <atomic>
#include <chrono>
#include <algorithm> // for std::clamp
#include <filesystem>
#include <fstream>
#include <system_error>
#include <thread>
#include <vector>
#if CICI_OS_MACOS
#include <cerrno>
#include <fcntl.h>
#include <spawn.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#elif CICI_OS_WINDOWS
#include <Windows.h>
#include <KnownFolders.h>
#include <shlobj.h>
#include <shellapi.h>
#else
#error "Unsupported platform"
#endif  //CICI_OS_MACOS

#include "log.h"
#include "cicibase.h"
#include "misc.h"
#include "nlohmann/json.hpp"

#if CICI_OS_MACOS
extern char** environ;
#endif

// Get the current dynamic library (.dylib) path.
#if CICI_OS_MACOS
static std::filesystem::path selfPath() {
    // Use the current function address to locate the current module.
    const void* current_addr = reinterpret_cast<const void*>(&selfPath);
    Dl_info info = {0};
    dladdr(current_addr, &info);
    std::filesystem::path p(info.dli_fname);
    return p.parent_path();
}

#elif CICI_OS_WINDOWS
static std::filesystem::path selfPath() {
	HMODULE hModule = 0;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
               reinterpret_cast<LPCWSTR>(selfPath), &hModule);
    wchar_t pathW[MAX_PATH] = {0};
    GetModuleFileNameW(hModule, pathW, MAX_PATH);
    std::filesystem::path p(pathW);
    
    return p.parent_path();
}

static std::filesystem::path defaultPath(REFKNOWNFOLDERID rfid) {
	PWSTR pszPathW = NULL;
	if (!SUCCEEDED(SHGetKnownFolderPath(rfid, 0, NULL, &pszPathW))) {
		assert(false);
		LogError << "SHGetKnownFolderPath failed" << logEnd;
		return ".\\";
	}

    std::filesystem::path path(pszPathW);
    CoTaskMemFree(pszPathW);

    assert(!path.empty());
    return path;
}
#else
#error "Unsupported platform"
#endif  //CICI_OS_MACOS

struct PathCollection {
    std::filesystem::path binPath_;
    std::filesystem::path bundlePath_;
    std::filesystem::path roamingAppDataPath_;
    std::filesystem::path logPath_;
    std::filesystem::path cachePath_;
    std::filesystem::path localCachePath_;
    std::filesystem::path remoteCachePath_;
    
    void initBundlePath() {
#if CICI_OS_MACOS
        std::filesystem::path soPath(binPath_);
        auto search = soPath.parent_path();
        while (search.has_parent_path() && search != search.root_path()) {
            std::error_code ec;
            if (std::filesystem::exists(search / "Contents/Info.plist", ec)) {
                bundlePath_ = search;
                return;
            }
            
            search = search.parent_path();
        }
#elif CICI_OS_WINDOWS
        bundlePath_ = binPath_;
#else
#error "Unsupported platform"
#endif
    }

    PathCollection() {
	    assert(binPath_.empty());
        binPath_ = selfPath();
        initBundlePath();
		roamingAppDataPath_ = roamingAppDataPath();
		logPath_ = logPath();
	    cachePath_ = cachePath();
		localCachePath_ = cachePath_ / "local";
	    remoteCachePath_ = cachePath_ / "remote";
    }
    
    static std::filesystem::path roamingAppDataPath() {
#if CICI_OS_MACOS
        const char* home = std::getenv("HOME");
        return std::filesystem::path(home) / "Library" / "Application Support";
#elif CICI_OS_WINDOWS
        return defaultPath(FOLDERID_RoamingAppData);
#else
#error "Unsupported platform"
#endif
    }

    static std::filesystem::path cachePath() {
#if CICI_OS_MACOS
        const char* home = std::getenv("HOME");
        auto p = std::filesystem::path(home) / "Library" / "Caches" / "cicibeauty";
        
        return p;
#elif CICI_OS_WINDOWS
	    return defaultPath(FOLDERID_LocalAppData) / "cicibeauty" / "cache";
#else
#error "Unsupported platform"
#endif
    }
    
    static std::filesystem::path logPath() {
#if CICI_OS_MACOS
        const char* home = std::getenv("HOME");
        auto p = std::filesystem::path(home) / "Library" / "Logs" / "cicibeauty";
        
        return p;
#elif CICI_OS_WINDOWS
	    return defaultPath(FOLDERID_LocalAppData) / "cicibeauty" / "logs";
#else
#error "Unsupported platform"
#endif
    }
};

PathCollection *gPathCollection = nullptr;

namespace {
struct PathInitializer {
    PathInitializer() { 
        assert(gPathCollection == nullptr);
        gPathCollection = new PathCollection;
    }
};
static void* init() {
	static PathInitializer i;
	return &i;
}

#if CICI_OS_WINDOWS
bool clearReadonlyAttribute(const std::filesystem::path& path) {
    DWORD attrs = GetFileAttributesW(path.wstring().c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
            return true;
        }
        LogError << "GetFileAttributesW failed: " << path.u8string() << " error:" << err << logEnd;
        return false;
    }

    if ((attrs & FILE_ATTRIBUTE_READONLY) == 0) {
        return true;
    }

    attrs &= ~FILE_ATTRIBUTE_READONLY;
    if (!SetFileAttributesW(path.wstring().c_str(), attrs)) {
        DWORD err = GetLastError();
        LogError << "SetFileAttributesW failed: " << path.u8string() << " error:" << err << logEnd;
        return false;
    }

    return true;
}

bool replaceFileAtomically(const std::filesystem::path& from, const std::filesystem::path& to) {
    if (std::filesystem::exists(to)) {
        clearReadonlyAttribute(to);
    }

    if (!MoveFileExW(from.wstring().c_str(), to.wstring().c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DWORD err = GetLastError();
        LogError << "MoveFileExW failed, from:" << from.u8string() << " to:" << to.u8string()
                 << " error:" << err << logEnd;
        return false;
    }

    return true;
}
#else
bool clearReadonlyAttribute(const std::filesystem::path&) {
    return true;
}

bool replaceFileAtomically(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    if (ec) {
        LogError << "rename failed, from:" << from.u8string() << " to:" << to.u8string()
                 << " error:" << ec.message() << logEnd;
        return false;
    }

    return true;
}
#endif

bool removeEntry(const std::filesystem::path& entry, bool* isDir = nullptr) {
    std::error_code ec;
    bool directory = std::filesystem::is_directory(entry, ec);
    if (ec) {
        LogError << "is_directory failed: " << entry.u8string() << " error:" << ec.message() << logEnd;
        return false;
    }

    if (isDir != nullptr) {
        *isDir = directory;
    }

    if (!clearReadonlyAttribute(entry)) {
        return false;
    }

    std::filesystem::remove(entry, ec);
    if (ec) {
        LogError << "remove failed: " << entry.u8string() << " error:" << ec.message() << logEnd;
        return false;
    }

    return true;
}

bool collectEntriesForRemoval(const std::filesystem::path& root, std::vector<std::filesystem::path>& entries) {
    entries.clear();

    std::error_code ec;
    std::filesystem::recursive_directory_iterator it(root, ec);
    if (ec) {
        LogError << "recursive_directory_iterator failed: " << root.u8string()
                 << " error:" << ec.message() << logEnd;
        return false;
    }

    for (const auto& item : it) {
        entries.push_back(item.path());
    }

    std::sort(entries.begin(), entries.end(), [](const std::filesystem::path& a, const std::filesystem::path& b) {
        return a.native().size() > b.native().size();
    });
    return true;
}

uint64_t currentTimeMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

uint64_t nextTempWriteSequence() {
    static std::atomic<uint64_t> sequence{0};
    return sequence.fetch_add(1, std::memory_order_relaxed) + 1;
}

std::filesystem::path tempWritePath(const std::filesystem::path& file) {
    auto parent = file.parent_path();
    auto name = file.filename().u8string();
    auto pid = std::to_string(ProcessThreadOp::pid());
    auto timestampMs = std::to_string(currentTimeMs());
    auto sequence = std::to_string(nextTempWriteSequence());
    std::u8string tmpName = name + u8".tmp." + to_u8(pid) + u8"." + to_u8(timestampMs) + u8"." + to_u8(sequence);
    return parent / tmpName;
}
}

bool UrlOp::isUrl(std::u8string_view url) {
    return url.starts_with(u8"http");
}

const std::filesystem::path& FileSystemOp::binPath() {
    init();
	return gPathCollection->binPath_;
}

const std::filesystem::path& FileSystemOp::resourcesPath() {
    static const auto path = []() {
        auto directory = binPath();
#if CICI_OS_MACOS
        directory = directory.parent_path();
#endif
        return directory / u8"Resources";
    }();
    return path;
}

const std::filesystem::path &FileSystemOp::bundlePath() {
	init();
	return gPathCollection->bundlePath_;
}

const std::filesystem::path& FileSystemOp::roamingAppDataPath() {
    init();
	return gPathCollection->roamingAppDataPath_;
}

const std::filesystem::path& FileSystemOp::logPath() {
    init();
	return gPathCollection->logPath_;
}

const std::filesystem::path& FileSystemOp::cachePath() {
    init();
	return gPathCollection->cachePath_;
}

const std::filesystem::path& FileSystemOp::localCachePath() {
    init();
	return gPathCollection->localCachePath_;
}

const std::filesystem::path& FileSystemOp::remoteCachePath() {
    init();
	return gPathCollection->remoteCachePath_;
}

bool FileSystemOp::replaceFileAtomically(const std::filesystem::path& from,
                                         const std::filesystem::path& to) {
    return ::replaceFileAtomically(from, to);
}

bool FileSystemOp::isFileExists(const std::filesystem::path& file) {
    std::error_code ec;
    bool b = std::filesystem::exists(file, ec);
    if (ec) {
        LogError<<ec.message()<<logEnd;
    }
    if (!b || ec) {
        return false;
    }
    
    b = std::filesystem::is_regular_file(file, ec);
    if (ec) {
        LogError<<ec.message()<<logEnd;
    }
    
    return b;
}

bool FileSystemOp::isFolderExists(const std::filesystem::path& folder) {
    std::error_code ec;
    bool b = std::filesystem::exists(folder, ec);
    if (ec) {
        LogError<<ec.message()<<logEnd;
    }
    if (!b || ec) {
        return false;
    }
    
    b = std::filesystem::is_directory(folder, ec);
    if (ec) {
        LogError<<ec.message()<<logEnd;
    }
    
    return b;
}

bool FileSystemOp::loadJson(const std::filesystem::path& path, nlohmann::json& j) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        LogError<<"open file:"<<path.u8string()<<" failed"<<logEnd;
        return false;
    }
    
    file.seekg(0, std::ios::end);
    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    BinaryStream buffer;
    buffer.resize(fileSize, 0);

    if (fileSize > 0) {
        file.read(&buffer[0], fileSize);
    }
    if (file.fail()) {
        LogError<<"read file:"<<path.u8string()<<" failed"<<logEnd;
	    return false;
    }
    
    assert(fileSize == file.gcount());
    assert(file.good());
    j = nlohmann::json::parse(buffer, nullptr, false);
    return true;
}

bool FileSystemOp::storeJson(const std::filesystem::path& path, const nlohmann::json& j) {
    std::string dumped = j.dump(4);
    std::u8string content = to_u8(dumped);
    return writeFile(path, content);
}

bool FileSystemOp::copyFile(const std::filesystem::path& srcPath, const std::filesystem::path& dstPath) {
    LogDebug << "copyFile from:" << srcPath.u8string() << " to:" << dstPath.u8string() << logEnd;
    if (auto parentDir = dstPath.parent_path(); !std::filesystem::exists(parentDir)) {
        if (!createDirectories(parentDir)) {
            return false;
        }
    }

    if (std::filesystem::exists(dstPath)) {
        clearReadonlyAttribute(dstPath);
    }

    std::error_code ec;
    std::filesystem::copy_file(srcPath, dstPath, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
	    LogError << "copy_file failed, from:" << srcPath.u8string() << " to:" << dstPath.u8string()
                 << " error:" << ec.message() << logEnd;
        return false;
    }

    return true;
}

bool FileSystemOp::copyDirectory(const std::filesystem::path& srcDir, const std::filesystem::path& dstDir,
                                  CopyDirProgressFuncU8 progress) {
    if (!std::filesystem::exists(srcDir)) {
        LogError << srcDir.u8string() << " does not exist" << logEnd;
        return false;
    }
        
    if (!std::filesystem::is_directory(srcDir)) {
        LogError<<srcDir.u8string()<<" is not directory"<<logEnd;
        return false;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(srcDir)) {
        std::filesystem::path dstPath = dstDir / entry.path().lexically_relative(srcDir);
        bool isDir = std::filesystem::is_directory(entry.path());
        if (std::filesystem::is_regular_file(entry.path())) {
            bool b = FileSystemOp::copyFile(entry.path(), dstPath);
            if (!b) {
                LogError<<"copyFile failed, from:"<<entry.path().u8string()<<" to:"<<dstPath.u8string()<<logEnd;
                return false;
            }
        }
        else if (isDir) {
            if (!std::filesystem::exists(dstPath)) {
                if (!createDirectories(dstPath)) {
                    return false;
                }
            }
        }
        if (progress) {
		progress(entry.path().lexically_relative(srcDir).u8string(), isDir);
        }
    }

    return true;
}

bool FileSystemOp::traverseDirectory(const std::filesystem::path &src, VisitorFuncU8 visitor) {
    if (!std::filesystem::exists(src)) {
        return false;
    }
        
    if (!std::filesystem::is_directory(src)) {
        return false;
    }

    visitor(u8"");    //root
    for (const auto& entry : std::filesystem::recursive_directory_iterator(src)) {
        visitor(entry.path().lexically_relative(src).u8string());
    }

    return true;
}

bool FileSystemOp::removeDir(const std::filesystem::path& path, RemoveDirProgressFuncU8 progress) {
    if (!std::filesystem::exists(path)) {
        return true;
    }

    std::vector<std::filesystem::path> entries;
    if (!collectEntriesForRemoval(path, entries)) {
        return false;
    }

    bool success = true;
    for (const auto& entry : entries) {
        bool isDir = false;
        if (!removeEntry(entry, &isDir)) {
            success = false;
        }
        if (progress) {
	        progress(entry.lexically_relative(path).u8string(), isDir);
        }
    }

    bool rootRemoved = removeEntry(path);
    if (!rootRemoved) {
        LogError << "remove root failed: " << path.u8string() << logEnd;
        success = false;
    }

    return success;
}

bool FileSystemOp::renameDir(const std::filesystem::path& oldName, const std::filesystem::path& newName) {
    std::error_code ec;
    std::filesystem::rename(oldName, newName, ec);

    bool suc = false;
    if (ec) {
        LogError<<ec.message()<<logEnd;
    } else {
        suc = true;
        //std::cout << "Rename/move succeeded" << std::endl;
    }
    
    return suc;
}

bool FileSystemOp::makeFileExecutable(const std::filesystem::path& file) {
#if CICI_OS_MACOS
    struct stat st;
    if (stat((file).string().c_str(), &st) != 0) {
        LogError<<"stat "<<(file).u8string()<<" failed"<<logEnd;
        return false;
    }
    
    mode_t new_mode = st.st_mode | S_IXUSR | S_IXGRP | S_IXOTH;
    if (chmod((file).string().c_str(), new_mode) == 0) {
        return true;
    }
    
    LogError<<"chmod failed"<<logEnd;
    return false;
#elif CICI_OS_WINDOWS
	if (!SetFileAttributesW(file.wstring().c_str(), FILE_ATTRIBUTE_NORMAL)) {
        return false;
    }
    
    std::filesystem::path streamPath = file;
    streamPath += ":Zone.Identifier";
    if (DeleteFileW(streamPath.wstring().c_str())) {
        return true;
    }

    DWORD err = GetLastError();
    return err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND;
#else
#error "Unsupported platform"
#endif
}

bool FileSystemOp::writeFile(const std::filesystem::path &file, uint8_t *data, uint32_t sz) {
    if (auto parentDir = file.parent_path(); !parentDir.empty() && !std::filesystem::exists(parentDir)) {
        if (!createDirectories(parentDir)) {
            return false;
        }
    }

    std::filesystem::path tmpFile = tempWritePath(file);
    std::error_code cleanupEc;

    std::ofstream ofs(tmpFile, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        LogError << "open temp file failed:" << tmpFile.u8string() << logEnd;
		return false;
	}
    ofs.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(sz));
    auto n = ofs.good() ? sz : 0;
    ofs.close();

	if (n != sz) {
		LogError << "write file error: expect " << sz << ", write " << n << logEnd;
        std::filesystem::remove(tmpFile, cleanupEc);
        return false;
	}

    if (!replaceFileAtomically(tmpFile, file)) {
        std::filesystem::remove(tmpFile, cleanupEc);
        return false;
    }

    return true;
}

// UTF-8 string versions
bool FileSystemOp::readFile(const std::filesystem::path &file, std::u8string &content) {
    std::ifstream ifs(file, std::ios::binary);
	if (!ifs.is_open()) {
        LogError << "open file failed:" << file.u8string() << logEnd;
		return false;
	}

    ifs.seekg(0, std::ios::end);
	auto sz = ifs.tellg();
    if (sz < 0) {
        LogError << "read file size failed:" << file.u8string() << logEnd;
        return false;
    }
    ifs.seekg(0, std::ios::beg);
	content.clear();
    content.resize(static_cast<size_t>(sz));
    if (sz > 0) {
        ifs.read(reinterpret_cast<char*>(content.data()), sz);
    }
    auto n = static_cast<size_t>(ifs.gcount());

    if (n != static_cast<size_t>(sz)) {
		LogError << "read file error: expect " << sz << ", read " << n << logEnd;
	}
    return n == static_cast<size_t>(sz);
}

bool FileSystemOp::writeFile(const std::filesystem::path &file, const std::u8string &content) {
	return writeFile(file, reinterpret_cast<uint8_t*>(const_cast<char8_t*>(content.data())), 
	                 static_cast<uint32_t>(content.size()));
}

bool FileSystemOp::appendFileAtomically(const std::filesystem::path& file, const std::u8string& content) {
    if (auto parentDir = file.parent_path(); !parentDir.empty() && !std::filesystem::exists(parentDir) &&
        !createDirectories(parentDir)) {
        return false;
    }
#if CICI_OS_WINDOWS
    HANDLE handle = CreateFileW(file.c_str(), GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        LogError << "open file for append failed:" << file.u8string() << " error:" << GetLastError() << logEnd;
        return false;
    }

    OVERLAPPED lockOffset{};
    if (!LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK, 0, MAXDWORD, MAXDWORD, &lockOffset)) {
        const DWORD error = GetLastError();
        CloseHandle(handle);
        LogError << "lock file for append failed:" << file.u8string() << " error:" << error << logEnd;
        return false;
    }

    DWORD written = 0;
    bool success = content.size() <= MAXDWORD;
    DWORD error = success ? ERROR_SUCCESS : ERROR_FILE_TOO_LARGE;
    LARGE_INTEGER distance{};
    if (success && !SetFilePointerEx(handle, distance, nullptr, FILE_END)) {
        success = false;
        error = GetLastError();
    } else if (success && !WriteFile(handle, content.data(), static_cast<DWORD>(content.size()), &written, nullptr)) {
        success = false;
        error = GetLastError();
    } else if (success && written != content.size()) {
        success = false;
        error = ERROR_WRITE_FAULT;
    }
    UnlockFileEx(handle, 0, MAXDWORD, MAXDWORD, &lockOffset);
    CloseHandle(handle);
    if (!success) {
        LogError << "append file failed:" << file.u8string() << " error:" << error << logEnd;
    }
    return success;
#else
    const int fd = open(file.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd == -1) {
        LogError << "open file for append failed:" << file.u8string() << " error:" << errno << logEnd;
        return false;
    }

    int lockResult;
    do {
        lockResult = flock(fd, LOCK_EX);
    } while (lockResult == -1 && errno == EINTR);
    if (lockResult == -1) {
        const int error = errno;
        close(fd);
        LogError << "lock file for append failed:" << file.u8string() << " error:" << error << logEnd;
        return false;
    }

    const char* data = reinterpret_cast<const char*>(content.data());
    size_t remaining = content.size();
    bool success = true;
    int error = 0;
    while (remaining > 0) {
        const ssize_t written = write(fd, data, remaining);
        if (written > 0) {
            data += written;
            remaining -= static_cast<size_t>(written);
        } else if (written == -1 && errno == EINTR) {
            continue;
        } else {
            success = false;
            error = written == -1 ? errno : EIO;
            break;
        }
    }
    flock(fd, LOCK_UN);
    close(fd);
    if (!success) {
        LogError << "append file failed:" << file.u8string() << " error:" << error << logEnd;
    }
    return success;
#endif
}

bool FileSystemOp::createDirectories(const std::filesystem::path &p) {
    std::error_code ec;
    if (std::filesystem::create_directories(p, ec)) {
        return true;
    }
    
    if (!ec) {
        return isFolderExists(p);
    }
    
    LogError<<"create_directories " << p.string() <<" failed:" <<ec.message() << "code:" <<ec.value() << logEnd;
    return false;
}

uint32_t ProcessThreadOp::pid() {
#if CICI_OS_MACOS
    return static_cast<uint32_t>(getpid());
#elif CICI_OS_WINDOWS
    return static_cast<uint32_t>(GetCurrentProcessId());
#else
#error "Unsupported platform"
#endif
}

uint64_t ProcessThreadOp::tid() {
#if CICI_OS_MACOS
    uint64_t tid = 0;
    pthread_threadid_np(nullptr, &tid);
    return tid;
#elif CICI_OS_WINDOWS
    return static_cast<uint64_t>(GetCurrentThreadId());
#else
#error "Unsupported platform"
#endif
}

namespace {

int launchProcess(const std::filesystem::path& executable,
                  const std::vector<std::u8string>& arguments, uint32_t& childPid,
                  const std::filesystem::path& workingDirectory, bool waitForExit) {
    childPid = 0;
#if CICI_OS_MACOS

    std::vector<std::u8string> argumentStorage;
    argumentStorage.reserve(arguments.size() + 1);
    argumentStorage.push_back(executable.u8string());
    argumentStorage.insert(argumentStorage.end(), arguments.begin(), arguments.end());

    std::vector<char*> argv;
    argv.reserve(argumentStorage.size() + 1);
    for (auto& argument : argumentStorage) {
        argv.push_back(reinterpret_cast<char*>(argument.data()));
    }
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_t* actionsPtr = nullptr;
    if (isOfficialBuild() || !workingDirectory.empty()) {
        int status = posix_spawn_file_actions_init(&actions);
        if (status != 0) {
            return status;
        }
        actionsPtr = &actions;

        if (!workingDirectory.empty()) {
            status = posix_spawn_file_actions_addchdir_np(actionsPtr, workingDirectory.c_str());
        }
        if (status == 0 && isOfficialBuild()) {
            status = posix_spawn_file_actions_addopen(actionsPtr, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
        }
        if (status == 0 && isOfficialBuild()) {
            status = posix_spawn_file_actions_addopen(
                actionsPtr, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
        }
        if (status != 0) {
            posix_spawn_file_actions_destroy(actionsPtr);
            return status;
        }
    }

    pid_t pid = 0;
    const int status = posix_spawn(
        &pid, executable.c_str(), actionsPtr, nullptr, argv.data(), environ);
    if (actionsPtr != nullptr) {
        posix_spawn_file_actions_destroy(actionsPtr);
    }
    if (status != 0) {
        return status;
    }

    childPid = static_cast<uint32_t>(pid);
    const auto wait = [pid]() {
        int childStatus = 0;
        pid_t result = 0;
        do {
            result = waitpid(pid, &childStatus, 0);
        } while (result < 0 && errno == EINTR);
        return result < 0 ? errno : 0;
    };
    if (waitForExit) {
        return wait();
    }
    // Preserve fire-and-forget behavior while ensuring the child is reaped after exit.
    std::thread(wait).detach();
    return 0;
#elif CICI_OS_WINDOWS
    // Quote each argument using the Windows CRT command-line rules, including
    // backslashes immediately before quotes and at the end of an argument.
    auto quote = [](const std::wstring& value) {
        std::wstring result = L"\"";
        size_t slashes = 0;
        for (wchar_t ch : value) {
            if (ch == L'\\') {
                ++slashes;
            } else {
                result.append(slashes * (ch == L'"' ? 2 : 1), L'\\');
                slashes = 0;
                if (ch == L'"') {
                    result += L'\\';
                }
                result += ch;
            }
        }
        result.append(slashes * 2, L'\\');
        result += L'"';
        return result;
    };
    std::wstring command = quote(executable.wstring());
    for (const auto& argument : arguments) {
        if (argument.find(u8'\0') != std::u8string::npos) {
            return ERROR_INVALID_PARAMETER;
        }
        command += L' ';
        command += quote(utf8ToWide(argument));
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        workingDirectory.empty() ? nullptr : workingDirectory.c_str(), &startup, &process)) {
        return static_cast<int>(GetLastError());
    }
    childPid = process.dwProcessId;
    CloseHandle(process.hThread);
    int error = 0;
    if (waitForExit && WaitForSingleObject(process.hProcess, INFINITE) == WAIT_FAILED) {
        error = static_cast<int>(GetLastError());
    }
    CloseHandle(process.hProcess);
    return error;
#endif
}

} // namespace

int ProcessThreadOp::launchDetached(const std::filesystem::path& executable,
                                    const std::vector<std::u8string>& arguments, uint32_t& childPid,
                                    const std::filesystem::path& workingDirectory) {
    return launchProcess(executable, arguments, childPid, workingDirectory, false);
}

int ProcessThreadOp::launchAndWait(const std::filesystem::path& executable,
                                  const std::vector<std::u8string>& arguments, uint32_t& childPid,
                                  const std::filesystem::path& workingDirectory) {
    return launchProcess(executable, arguments, childPid, workingDirectory, true);
}

#if CICI_OS_WINDOWS
std::wstring utf8ToWide(const std::u8string& s) {
    if (s.empty()) {
        return L"";
    }
    auto p = reinterpret_cast<const char*>(s.c_str());
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, p, -1, nullptr, 0);
    if (size_needed <= 0) {
        return L"";
    }
    std::wstring ws(static_cast<size_t>(size_needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, p, -1, ws.data(), size_needed);
    ws.pop_back();
    return ws;
}

std::u8string wideToUtf8(const std::wstring& ws) {
    if (ws.empty()) {
        return u8"";
    }
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size_needed <= 0) {
        return u8"";
    }
    BinaryStream s(static_cast<size_t>(size_needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, s.data(), size_needed, nullptr, nullptr);
    s.pop_back();
    return std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size());
}
#endif
