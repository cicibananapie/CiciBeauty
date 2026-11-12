# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: MIT
# See LICENSES/MIT.txt for the full license text.

cmake_minimum_required(VERSION 3.22)
project(cicibeauty LANGUAGES C CXX)

set(CICI_PUBLIC_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(3RD_DIR "${CICI_PUBLIC_SOURCE_DIR}/3rd")
include(cmake/CiciBuild.cmake)
include(cmake/CiciObs.cmake)

set(CICI_CHANNEL_ID "$ENV{CICI_CHANNEL_ID}" CACHE STRING "Plugin distribution channel")
configure_file(cicibeauty/cicibeautyconfigure.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/cicibeautyconfigure.h" @ONLY)

if(CICI_BUILD_TESTS)
    enable_testing()
endif()
add_subdirectory(cicibase)
add_subdirectory(protocol)
add_subdirectory(cicibeauty)
add_subdirectory(cicibeautyStub)

set(CICI_LOCAL_BUNDLE_ROOT "" CACHE PATH "macOS local development bundle output root")
if(CICI_LOCAL_BUNDLE_ROOT)
    if(NOT CICI_OS_MACOS OR NOT CICI_GEN_XCODE OR OFFICIAL_BUILD)
        message(FATAL_ERROR "CICI_LOCAL_BUNDLE_ROOT requires a non-official macOS Xcode build")
    endif()
    set(_local_contents "${CICI_LOCAL_BUNDLE_ROOT}/$<CONFIG>/ciciinstaller.app/Contents")
    set_target_properties(cicibeauty PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${_local_contents}/Frameworks"
        XCODE_SCHEME_ENVIRONMENT "DYLD_LIBRARY_PATH=$(TARGET_BUILD_DIR)"
    )
    set_target_properties(cicibeautyStub PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${_local_contents}/Helpers"
    )
endif()

if(CICI_BUILD_TESTS)
    add_subdirectory(tests/ipcwrapper)
    add_subdirectory(tests/feedbackdialog)
    add_subdirectory(tests/updateui)
    add_subdirectory(tests/obsformatsource)
endif()
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
include(cmake/CiciIDE.cmake)
