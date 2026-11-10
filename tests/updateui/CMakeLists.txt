# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: GPL-2.0-or-later
# See LICENSES/GPL-2.0-or-later.txt for the full license text.

cmake_minimum_required(VERSION 3.16)
project(cici-updateui-tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(CICI_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
list(PREPEND CMAKE_PREFIX_PATH "${CICI_REPOSITORY_ROOT}/depends/obsQT")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_executable(updateui_display_tests
    updateui_display_tests.cpp
    "${CICI_REPOSITORY_ROOT}/cicibeauty/update/updatedescriptionpages.cpp"
    "${CICI_REPOSITORY_ROOT}/cicibeauty/update/updatedescriptionpages.h"
)

target_include_directories(updateui_display_tests PRIVATE
    "${CICI_REPOSITORY_ROOT}/cicibeauty/update"
)

target_link_libraries(updateui_display_tests PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

if(WIN32)
    add_custom_command(TARGET updateui_display_tests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE_DIR:updateui_display_tests>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:Qt6::Gui>" "$<TARGET_FILE_DIR:updateui_display_tests>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:Qt6::Widgets>" "$<TARGET_FILE_DIR:updateui_display_tests>"
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "$<TARGET_FILE_DIR:updateui_display_tests>/platforms"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:Qt6::QMinimalIntegrationPlugin>"
            "$<TARGET_FILE_DIR:updateui_display_tests>/platforms"
        VERBATIM
    )
endif()

enable_testing()
add_test(NAME updateui_display_tests COMMAND updateui_display_tests)
set_tests_properties(updateui_display_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=minimal"
)
