# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: MIT
# See LICENSES/MIT.txt for the full license text.

set(CICI_DEPENDENCIES_DIR "${CMAKE_SOURCE_DIR}/depends" CACHE PATH "Downloaded public build dependencies")
set(CICI_OBS_SDK_ROOT
    "${CICI_DEPENDENCIES_DIR}/obs-sdk"
    CACHE PATH
    "Root of the immutable OBS Plugin SDK dependency"
)
set(libobs_INCLUDE_DIRS "${CICI_OBS_SDK_ROOT}/include")
set(frontendapi_INCLUDE_DIRS "${CICI_OBS_SDK_ROOT}/include")
set(libobs_LIB_DIRS "${CICI_OBS_SDK_ROOT}/lib")
set(CICI_OBS_LIBRARY_DIRS
    "$<$<CONFIG:Debug>:${libobs_LIB_DIRS}/Debug>"
    "$<$<NOT:$<CONFIG:Debug>>:${libobs_LIB_DIRS}/Release>"
)

set(_cici_obs_sdk_required_files
    "include/obs.h"
    "include/obs-module.h"
    "include/obs-frontend-api.h"
)
set(_cici_obs_runtime_default "$ENV{CICI_OBS_RUNTIME_DIR}")
if(NOT _cici_obs_runtime_default AND CICI_OS_MACOS)
    set(_cici_obs_runtime_default "/Applications/OBS.app/Contents/Frameworks")
endif()
set(CICI_OBS_RUNTIME_DIR "${_cici_obs_runtime_default}" CACHE PATH
    "Installed OBS runtime directory used by tests (Frameworks on macOS, bin/64bit on Windows)")

if(CICI_OS_WINDOWS)
    list(APPEND _cici_obs_sdk_required_files
        "lib/Debug/obs.lib"
        "lib/Debug/obs-frontend-api.lib"
        "lib/Release/obs.lib"
        "lib/Release/obs-frontend-api.lib"
    )
elseif(CICI_OS_MACOS)
    set(CICI_OBS_FRONTEND_LIB_DIRS "${libobs_LIB_DIRS}")
    list(APPEND _cici_obs_sdk_required_files
        "lib/Debug/libobs.framework"
        "lib/Debug/obs-frontend-api.dylib"
        "lib/Release/libobs.framework"
        "lib/Release/obs-frontend-api.dylib"
    )
endif()
foreach(_relative_path IN LISTS _cici_obs_sdk_required_files)
    if(NOT EXISTS "${CICI_OBS_SDK_ROOT}/${_relative_path}")
        message(FATAL_ERROR
            "OBS Plugin SDK is incomplete for ${CMAKE_SYSTEM_NAME}: "
            "${CICI_OBS_SDK_ROOT}/${_relative_path}. "
            "Run scripts/cici.py with --pull-deps before configuring the project."
        )
    endif()
endforeach()
include("${CICI_PUBLIC_SOURCE_DIR}/cmake/CiciQt.cmake")

