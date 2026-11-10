# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: GPL-2.0-or-later
# See LICENSES/GPL-2.0-or-later.txt for the full license text.

set(TargetName "feedbackdialog_tests")

find_package(Qt6 COMPONENTS Core Gui Widgets)
if(Qt6_FOUND)
    set(CICI_TEST_QT_LIBS Qt6::Core Qt6::Gui Qt6::Widgets)
else()
    find_package(Qt5 5.15 COMPONENTS Core Gui Widgets REQUIRED)
    set(CICI_TEST_QT_LIBS Qt5::Core Qt5::Gui Qt5::Widgets)
endif()

add_executable(${TargetName}
    feedbackdialog_tests.cpp
    ${CICI_PUBLIC_SOURCE_DIR}/cicibeauty/lumaclient.cpp
    ${CICI_PUBLIC_SOURCE_DIR}/cicibeauty/feedback/feedbackclient.cpp
    ${CICI_PUBLIC_SOURCE_DIR}/cicibeauty/feedback/feedbackclient.h
    ${CICI_PUBLIC_SOURCE_DIR}/cicibeauty/feedback/feedbackdialog.cpp
    ${CICI_PUBLIC_SOURCE_DIR}/cicibeauty/feedback/feedbackdialog.h
)

set_target_properties(${TargetName} PROPERTIES AUTOMOC ON)

target_precompile_headers(${TargetName} PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CICI_PUBLIC_SOURCE_DIR}/cicibase/include/ciciglobal_pch.h>"
)

target_include_directories(${TargetName} PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CICI_PUBLIC_SOURCE_DIR}/cicibeauty
    ${CMAKE_SOURCE_DIR}/3rd/include
    ${libobs_INCLUDE_DIRS}
)

target_link_libraries(${TargetName} PRIVATE cicibase ${CICI_TEST_QT_LIBS})

if(CICI_OS_WINDOWS)
    target_link_directories(${TargetName} PRIVATE
        ${CICI_OBS_LIBRARY_DIRS}
    )
    target_link_libraries(${TargetName} PRIVATE obs)
elseif(CICI_OS_MACOS)
    target_link_libraries(${TargetName} PRIVATE
        $<$<CONFIG:Debug>:${libobs_LIB_DIRS}/Debug/libobs.framework>
        $<$<CONFIG:Release>:${libobs_LIB_DIRS}/Release/libobs.framework>
        $<$<CONFIG:RelWithDebInfo>:${libobs_LIB_DIRS}/Release/libobs.framework>
    )
endif()

cici_apply_debug_symbol_policy(${TargetName})

add_test(NAME ${TargetName} COMMAND ${TargetName})
set_tests_properties(${TargetName} PROPERTIES TIMEOUT 30)
if(CICI_OS_WINDOWS)
    set(_cici_feedback_test_path "${QT_PATH}/bin")
    if(CICI_OBS_RUNTIME_DIR)
        string(APPEND _cici_feedback_test_path "\\;${CICI_OBS_RUNTIME_DIR}")
    endif()
    set_tests_properties(${TargetName} PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=minimal;QT_PLUGIN_PATH=${QT_PATH}/plugins;PATH=${_cici_feedback_test_path}"
    )
elseif(CICI_OS_MACOS)
    set_tests_properties(${TargetName} PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=minimal;QT_STYLE_OVERRIDE=Fusion;DYLD_FRAMEWORK_PATH=${QT_PATH}/lib:${CICI_OBS_RUNTIME_DIR};DYLD_LIBRARY_PATH=${CICI_OBS_RUNTIME_DIR}"
    )
endif()
