# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: MIT
# See LICENSES/MIT.txt for the full license text.

set(base_FILES
    include/ciciglobal_pch.h
    include/cicibase.h
    include/sync.h
    include/hptimer.h
    include/uuid.h
    include/log.h
    include/gcd.h
    include/cicilocale.h
    include/obscompat.h
    include/misc.h
    include/imageutility.h
    include/jsonutility.h
    include/mediatype.h
    include/binarymsg.h
    include/ipcwrapper.h
    include/randwrapper.h
    include/shmem.h
    include/shmframe.h
    include/timeutility.h
    include/version_slot.h
    include/stackstring.h
    include/install_layout.h
    include/protocol/update_common.h
    include/protocol/feedback_common.h
    src/cicibase.cpp
    src/log.cpp
    src/cicilocale.cpp
    src/obscompat.cpp
    src/misc.cpp
    src/stbimageimpl.cpp
    src/imageutility.cpp
    src/mediatype.cpp
    src/gcd.cpp
    src/sync.cpp
    src/uuid.cpp
    src/shmem.cpp
    src/shmframe.cpp
    src/ipcwrapper.cpp
    src/randwrapper.cpp
    src/timeutility.cpp
)

if(CICI_OS_MACOS)
    list(APPEND base_FILES macos/cicilocale.mm)
elseif(CICI_OS_WINDOWS)
    list(APPEND base_FILES windows/cicilocale.cpp)
endif()
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${base_FILES})

add_library(cicibase STATIC ${base_FILES})
set(base_TARGETS cicibase)
if(CICI_OS_WINDOWS)
    add_library(cicibase_mt STATIC ${base_FILES})
    set_target_properties(cicibase_mt PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    list(APPEND base_TARGETS cicibase_mt)
endif()

foreach(target IN LISTS base_TARGETS)
    cici_apply_debug_symbol_policy(${target})
    target_include_directories(${target} PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/protocol"
        "${CICI_PUBLIC_SOURCE_DIR}/3rd/include"
    )
    target_link_libraries(${target} PRIVATE ciciglobal_pch)
    set_target_properties(${target} PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CICI_ARCHIVE_OUTPUT_ROOT}"
    )
    if(CICI_OS_MACOS)
        target_link_libraries(${target} PRIVATE "-framework Foundation")
        set_target_properties(${target} PROPERTIES
            XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC "YES"
        )
    elseif(CICI_OS_WINDOWS)
        target_link_libraries(${target} PRIVATE shell32 ole32 user32)
    endif()
endforeach()

if(CICI_BUILD_TESTS)
    add_executable(cicibase_image_path_tests tests/imageutility_path_tests.cpp)
    target_compile_definitions(cicibase_image_path_tests PRIVATE
        CICI_TEST_IMAGE_PATH="${CICI_PUBLIC_SOURCE_DIR}/cicibeauty/Resources/square@1x.png"
    )
    add_executable(cicibase_log_path_tests tests/log_path_tests.cpp)
    add_executable(cicibase_process_tests tests/process_tests.cpp)
    add_executable(cicibase_shmframe_tests tests/shmframe_tests.cpp)
    foreach(target IN ITEMS cicibase_image_path_tests cicibase_log_path_tests cicibase_process_tests cicibase_shmframe_tests)
        target_link_libraries(${target} PRIVATE cicibase ciciglobal_pch)
        cici_apply_debug_symbol_policy(${target})
        add_test(NAME ${target} COMMAND ${target})
        set_tests_properties(${target} PROPERTIES TIMEOUT 30)
    endforeach()
endif()
