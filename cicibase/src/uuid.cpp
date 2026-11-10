// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#include <assert.h>

#if CICI_OS_MACOS
#include <uuid/uuid.h>
#elif CICI_OS_WINDOWS
#include <objbase.h>
#pragma comment(lib, "ole32.lib")
#else
#error "Unsupported platform"
#endif

#include "uuid.h"

std::u8string CiciUUID::generate() {
    char buffer[37] = {0};
#if CICI_OS_MACOS
    uuid_t uuid;
    uuid_generate(uuid);
    uuid_unparse(uuid, buffer);
#elif CICI_OS_WINDOWS
    GUID guid;  //CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);CoUninitialize();
    if (CoCreateGuid(&guid) != S_OK) {
        assert(false);
        return u8"";
    }
    sprintf_s(buffer, "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
        guid.Data1, guid.Data2, guid.Data3,
        guid.Data4[0], guid.Data4[1],
        guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5],
        guid.Data4[6], guid.Data4[7]);
#else
#error "Unsupported platform"
#endif
    
    return to_u8(buffer);
}
