// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <new>
#include <time.h>
#include <string>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <obs-module.h> // Required core OBS header.
#if CICI_OS_MACOS
#include <sys/time.h>
#include <dlfcn.h>
#include <dirent.h>
#elif CICI_OS_WINDOWS
#include <Windows.h>
#else
#error "Unsupported platform"
#endif  //CICI_OS_MACOS

#include "ciciconfigure.h"
#include "ciciplugin.h"
#include "install_layout.h"
#include "log.h"
#include "misc.h"
#include "obscompat.h"
#include "version_slot.h"

const char8_t* kConfigFileName1 = u8"cicibeautyconfig";
const char8_t *kConfigFileName2 = u8"config";

namespace {

const char* kDefaultLocale = "en_US";
std::string currentLocale = kDefaultLocale;
ICiciBeautyPlugin* pCiciPlugin = nullptr;

const char* obsLocaleToCiciLocale(const char* locale) {
    if (locale == nullptr || locale[0] == '\0') {
        return kDefaultLocale;
    }

    if (strcmp(locale, "zh-CN") == 0) {
        return "zh_CN";
    }

    if (strcmp(locale, "zh-TW") == 0) {
        return "zh_TW";
    }

    if (strcmp(locale, "en-US") == 0) {
        return "en_US";
    }

    return kDefaultLocale;
}

void notifyLocaleChanged() {
    if (pCiciPlugin != nullptr) {
        pCiciPlugin->onLocaleChanged(currentLocale.c_str());
    }
}

bool checkSupportedOBSVersion() {
    const char* runtimeVersion = obs_get_version_string();
    const std::u8string runtimeVersionU8 = runtimeVersion != nullptr ? to_u8(runtimeVersion) : std::u8string();
    const std::u8string minimumVersion = CiciOBSCompat::minimumSupportedVersionString();

    if (!CiciOBSCompat::isSupportedVersion(runtimeVersionU8)) {
        LogError << "unsupported OBS version, runtime=" << runtimeVersionU8
                 << " minimum_supported=" << minimumVersion << logEnd;
        return false;
    }

    return true;
}

}

bool entryModulePath(const std::filesystem::path& p, std::vector<std::u8string>& candidates) {
    auto p1 = p / kConfigFileName1;
    std::u8string ciciPath;
    if (!readTrimmedTextFile(p1, ciciPath)) {
        LogError<<"readTrimmedTextFile failed:" <<p1.u8string()<<logEnd;
        return false;
    }
    
    LogInfo<<ciciPath<<logEnd;
    std::filesystem::path installRoot(ciciPath);
    std::u8string activeSlot;
    if (!readActiveInstallSlotName(installRoot, activeSlot, kConfigFileName2)) {
        LogWarn << "active slot config missing, fallback to slot scan" << logEnd;
    }

    if (!activeSlot.empty() && slotContainsInstallEntryModule(installRoot, activeSlot)) {
        auto modulePath = installSlotEntryModulePathForName(installRoot, activeSlot);
        candidates.push_back(modulePath.u8string());
        LogInfo << "active slot:" << modulePath.u8string() << logEnd;
    }

    std::vector<std::u8string> slots;
    enumerateInstallSlots(installRoot, slots);
    std::sort(slots.begin(), slots.end(), versionSlotGreater);

    for (const auto& slotName : slots) {
        if (!activeSlot.empty() && slotName == activeSlot) {
            continue;
        }

        auto modulePath = installSlotEntryModulePathForName(installRoot, slotName);
        candidates.push_back(modulePath.u8string());
        LogInfo << "available slot:" << modulePath.u8string() << logEnd;
    }

    if (candidates.empty()) {
        LogError << "available install slots are empty" << logEnd;
        return false;
    }

    return true;
}

extern "C" {
#if CICI_OS_MACOS
    void *soHandle;
    void *dlopen2(const char8_t *filename, int flags) {
        return dlopen(to_char(filename), flags);
    }
#elif CICI_OS_WINDOWS
    HMODULE soHandle;
    const int RTLD_LAZY = 0;
    HMODULE dlopen2(const char8_t* path, int flag) {
        (void)flag;
	    if (path == nullptr || path[0] == '\0') {
            LogError<<"invalid dll path"<<logEnd;
		    return 0;
	    }

        constexpr DWORD loadFlags = LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                    LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
                                    LOAD_LIBRARY_SEARCH_SYSTEM32;
        HMODULE h = LoadLibraryExW(utf8ToWide(path).c_str(), NULL, loadFlags);
        const auto error = h == nullptr ? GetLastError() : ERROR_SUCCESS;
        
        if (h == 0) {
		    LogError << "LoadLibraryExW " << path << " failed:" << error << logEnd;
        }

        return h;
    }

    FARPROC dlsym(HMODULE handle, const char* name) {
	    return GetProcAddress(handle, name);
    }

    void dlclose(HMODULE handle) {
	    FreeLibrary(handle);
    }

    std::u8string errBuf;
    const char* dlerror() {
	    DWORD code = GetLastError();
	    if (code == 0) {
		    return 0;
        }

        wchar_t wbuf[1024] = {0};
        DWORD len = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, 0, code, 0, wbuf, 1024, 0);
        if (len > 0) {
            errBuf = wideToUtf8(wbuf);
            return to_char(errBuf);
        }
        return "Unknow Error";
    }
#else
#error "Unsupported platform"
#endif
    
    // 4. Plugin entry point called when OBS loads the module.
    OBS_DECLARE_MODULE()

    void obs_module_set_locale(const char* locale) {
        currentLocale = obsLocaleToCiciLocale(locale);
        notifyLocaleChanged();
    }

    void obs_module_free_locale(void) {
        currentLocale = kDefaultLocale;
    }

    bool obs_module_load(void) {
        auto p = FileSystemOp::bundlePath();
#if CICI_OS_MACOS
        p = p.parent_path();
#endif
        initCiciLogConfig(ciciDefaultLogLevel(), u8"cicibeautystub.log");
		LogInfo << "CiciBeauty Filter load begin!" << logEnd;
	#if CICI_OS_MACOS
		const char* dyldLibraryPath = getenv("DYLD_LIBRARY_PATH");
		LogInfo << "DYLD_LIBRARY_PATH=" << (dyldLibraryPath != nullptr ? dyldLibraryPath : "") << logEnd;
	#endif
        if (!checkSupportedOBSVersion()) {
            return false;
        }

        std::vector<std::u8string> candidates;
        if (!entryModulePath(p, candidates)) {
            LogError<<"entryModulePath failed"<<logEnd;
            return false;
        }
        for (size_t i = 0; i < candidates.size(); i++) {
            const auto& item = candidates[i];
            LogInfo<<item<<logEnd;
            soHandle = dlopen2(item.c_str(), RTLD_LAZY);
            if (soHandle != 0) {
                break;
            }
        }
        
        if (soHandle == 0) {
            LogError<<dlerror()<<logEnd;
            assert(false);
            return false;
        }
        
        typedef ICiciBeautyPlugin* (*Func)(void);
        Func f = reinterpret_cast<Func>(dlsym(soHandle, "ciciPlugin"));
        if (f == NULL) {
            LogError<<"dlsym failed"<<logEnd;
            assert(false);
            dlclose(soHandle);
            return false;
        }
        
        pCiciPlugin = f();
        if (pCiciPlugin == nullptr) {
            LogError<<"f() failed"<<logEnd;
            assert(false);
            dlclose(soHandle);
            soHandle = 0;
            return false;
        }
        
        notifyLocaleChanged();
        bool r = pCiciPlugin->onLoad();
        LogInfo<<"CiciBeauty Filter load end!"<<logEnd;
        return r;
    }
    
    // Optional plugin unload callback.
    void obs_module_unload(void) {
        LogInfo<<"CiciBeauty Filter unload begin!"<<logEnd;

        if (pCiciPlugin != nullptr) {
            pCiciPlugin->onUnLoad();
            pCiciPlugin = nullptr;
        }

        if (soHandle != 0) {
            dlclose(soHandle);
            soHandle = 0;
        }
        LogInfo<<"CiciBeauty Filter unload end!"<<logEnd;
    }
    
}
