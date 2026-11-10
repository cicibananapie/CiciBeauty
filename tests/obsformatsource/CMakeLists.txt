# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: GPL-2.0-or-later
# See LICENSES/GPL-2.0-or-later.txt for the full license text.

set(TargetName "ciciobsformattest")

add_library(${TargetName} MODULE obsformatsource.cpp)

target_include_directories(${TargetName} PRIVATE
    ${libobs_INCLUDE_DIRS}
    ${frontendapi_INCLUDE_DIRS}
)

if(CICI_OS_WINDOWS)
    target_link_directories(${TargetName} PRIVATE
        ${CICI_OBS_LIBRARY_DIRS}
    )
    target_link_libraries(${TargetName} PRIVATE obs obs-frontend-api)
elseif(CICI_OS_MACOS)
    set(MACOSX_BUNDLE_EXECUTABLE_NAME "${TargetName}")
    set(MACOSX_BUNDLE_BUNDLE_NAME "${TargetName}")
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.cicibeauty.${TargetName}")
    set_target_properties(${TargetName} PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "plugin"
        MACOSX_BUNDLE_INFO_PLIST
            "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER
            "com.cicibeauty.${TargetName}"
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
    )
    target_link_libraries(${TargetName} PRIVATE
        $<$<CONFIG:Debug>:${CICI_OBS_FRONTEND_LIB_DIRS}/Debug/obs-frontend-api.dylib>
        $<$<CONFIG:Release>:${CICI_OBS_FRONTEND_LIB_DIRS}/Release/obs-frontend-api.dylib>
        $<$<CONFIG:RelWithDebInfo>:${CICI_OBS_FRONTEND_LIB_DIRS}/Release/obs-frontend-api.dylib>
        $<$<CONFIG:Debug>:${libobs_LIB_DIRS}/Debug/libobs.framework>
        $<$<CONFIG:Release>:${libobs_LIB_DIRS}/Release/libobs.framework>
        $<$<CONFIG:RelWithDebInfo>:${libobs_LIB_DIRS}/Release/libobs.framework>
    )
else()
    message(FATAL_ERROR "Unsupported platform for ${TargetName}")
endif()

set_target_properties(${TargetName} PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CICI_RUNTIME_OUTPUT_ROOT}"
)

cici_apply_debug_symbol_policy(${TargetName})
