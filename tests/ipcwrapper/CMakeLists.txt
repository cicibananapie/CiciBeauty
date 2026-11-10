# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: MIT
# See LICENSES/MIT.txt for the full license text.

set(TargetName "ipcwrapper_tests")

add_executable(${TargetName}
    ipcwrapper_tests.cpp
)

target_include_directories(${TargetName} PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/3rd/include
)

target_link_libraries(${TargetName} PRIVATE cicibase)

if(CICI_OS_MACOS)
    target_link_libraries(${TargetName} PRIVATE "-framework CoreFoundation")
endif()

cici_apply_debug_symbol_policy(${TargetName})

add_test(NAME ${TargetName} COMMAND ${TargetName})
set_tests_properties(${TargetName} PROPERTIES TIMEOUT 30)
