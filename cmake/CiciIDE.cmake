# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: MIT
# See LICENSES/MIT.txt for the full license text.

if(CICI_GEN_XCODE)
    set_property(GLOBAL PROPERTY AUTOGEN_TARGETS_FOLDER "Scripts/Autogen")
    # Separate production targets, tests, and generated build helpers.
    function(cici_group_xcode_targets directory)
        get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
        foreach(target IN LISTS targets)
            get_target_property(folder ${target} FOLDER)
            if(target MATCHES "[Tt]ests$" OR target STREQUAL "ciciobsformattest")
                set_property(TARGET ${target} PROPERTY FOLDER "Tests")
            elseif(target MATCHES "^(UpdateTranslations|Translations|ui2header)$")
                set_property(TARGET ${target} PROPERTY FOLDER "Scripts")
            elseif(NOT folder)
                set_property(TARGET ${target} PROPERTY FOLDER "Targets")
            endif()
        endforeach()
        get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
        foreach(subdirectory IN LISTS subdirectories)
            cici_group_xcode_targets("${subdirectory}")
        endforeach()
    endfunction()
    cici_group_xcode_targets("${CMAKE_CURRENT_SOURCE_DIR}")
endif()

if(CICI_OS_WINDOWS AND CICI_BUILD_TESTS)
    # Keep test projects out of the Visual Studio solution root. Tests live in
    # several production subdirectories, so group them after every target has
    # been created instead of requiring each subdirectory to repeat FOLDER.
    function(cici_group_windows_test_targets directory)
        get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
        foreach(target IN LISTS targets)
            if(target MATCHES "[Tt]ests?$" OR target MATCHES "_probe$")
                set_property(TARGET ${target} PROPERTY FOLDER "test")
            endif()
        endforeach()

        get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
        foreach(subdirectory IN LISTS subdirectories)
            cici_group_windows_test_targets("${subdirectory}")
        endforeach()
    endfunction()
    cici_group_windows_test_targets("${CMAKE_CURRENT_SOURCE_DIR}")
endif()
