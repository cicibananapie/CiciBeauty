# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: MIT
# See LICENSES/MIT.txt for the full license text.

# ------------------------------------------------------------------------------
# CICI unified platform/toolchain variables for CMake.
# ------------------------------------------------------------------------------
# Convention: CICI_OS_* values are always 0/1 so they can be used for:
# - CMake conditions: if(CICI_OS_WINDOWS) / if(CICI_OS_MACOS)
# - Preprocessor macros: CICI_OS_WINDOWS / CICI_OS_MACOS, injected via compile definitions.
set(CICI_OS_WINDOWS 0)
set(CICI_OS_MACOS 0)

if(WIN32)
    set(CICI_OS_WINDOWS 1)
elseif(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(CICI_OS_MACOS 1)
endif()

set(CICI_GEN_XCODE 0)
if(XCODE)
    set(CICI_GEN_XCODE 1)
endif()

set(CICI_IDE_MSVC 0)
if(MSVC_IDE)
    set(CICI_IDE_MSVC 1)
endif()

set(CICI_COMPILER_MSVC 0)
if(MSVC)
    set(CICI_COMPILER_MSVC 1)
endif()

set(CICI_COMPILER_CLANGCL 0)
if(MSVC AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CICI_COMPILER_CLANGCL 1)
endif()

if(NOT CICI_OS_WINDOWS AND NOT CICI_OS_MACOS)
    message(FATAL_ERROR "Unsupported platform: only Windows and macOS are supported")
endif()

if(CICI_OS_MACOS AND NOT CICI_GEN_XCODE)
    message(FATAL_ERROR "macOS builds support only the Xcode generator; configure CMake with -G Xcode")
endif()

option(CICI_ENABLE_ASAN "Enable AddressSanitizer for Debug builds" OFF)
option(CICI_BUILD_TESTS "Build cici automated tests" OFF)

# Keep every build tree self-contained. This prevents MSVC, ClangCL, ASAN and
# packaging builds from overwriting one another's binaries and import libraries.
set(CICI_OUTPUT_ROOT
    "${CMAKE_BINARY_DIR}/artifacts"
    CACHE PATH
    "Root directory for build-tree-specific runtime and library outputs"
)
set(CICI_RUNTIME_OUTPUT_ROOT "${CICI_OUTPUT_ROOT}/bin")
set(CICI_ARCHIVE_OUTPUT_ROOT "${CICI_OUTPUT_ROOT}/lib")
if(CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_GENERATOR STREQUAL "Xcode")
    set(CICI_ARCHIVE_LINK_DIRECTORY "${CICI_ARCHIVE_OUTPUT_ROOT}/$<CONFIG>")
else()
    set(CICI_ARCHIVE_LINK_DIRECTORY "${CICI_ARCHIVE_OUTPUT_ROOT}")
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CICI_RUNTIME_OUTPUT_ROOT}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CICI_RUNTIME_OUTPUT_ROOT}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CICI_ARCHIVE_OUTPUT_ROOT}")

if(CICI_OS_WINDOWS)
    set(CICI_PDB_OUTPUT_ROOT
        "${CMAKE_BINARY_DIR}/symbols"
        CACHE PATH
        "Directory below which configuration-specific linker PDB files are written"
    )
endif()

# Inject platform macros into every target so targets do not depend on PCH availability/linkage.
add_compile_definitions(
    CICI_OS_WINDOWS=${CICI_OS_WINDOWS}
    CICI_OS_MACOS=${CICI_OS_MACOS}
)

# A warning in project source must fail the build so it cannot be hidden in
# parallel compiler output or mistaken for a successful release build.
if(CICI_COMPILER_MSVC)
    add_compile_options(/WX)
elseif(CICI_OS_MACOS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Werror)
endif()

# 1. Version and official build logic.
if(NOT CICI_VERSION OR CICI_VERSION STREQUAL "0.0.0")
    set(CICI_VERSION "0.0.0")
    set(CICI_OFFICIAL_BUILD_DEFAULT OFF)
else()
    set(CICI_OFFICIAL_BUILD_DEFAULT ON)
endif()

# A versioned build is official by default, but an explicit command-line value wins.
option(OFFICIAL_BUILD "official build" ${CICI_OFFICIAL_BUILD_DEFAULT})

set(CICI_MIN_SUPPORTED_OBS_VERSION "30.1.1" CACHE STRING "Minimum supported OBS version")

include(CheckIPOSupported)
check_ipo_supported(RESULT lto_supported OUTPUT lto_error)

if(lto_supported AND NOT CICI_ENABLE_ASAN)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
endif()

if(CICI_ENABLE_ASAN AND CICI_COMPILER_MSVC)
    message(STATUS "CICI_ENABLE_ASAN=ON: enabling MSVC AddressSanitizer for Debug and disabling IPO")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)
    add_compile_options(
        "$<$<AND:$<CONFIG:Debug>,$<COMPILE_LANGUAGE:C>>:/fsanitize=address>"
        "$<$<AND:$<CONFIG:Debug>,$<COMPILE_LANGUAGE:CXX>>:/fsanitize=address>"
    )
    add_link_options("$<$<CONFIG:Debug>:/INCREMENTAL:NO>")
    add_compile_definitions("$<$<CONFIG:Debug>:CICI_ENABLE_ASAN=1>")
elseif(CICI_ENABLE_ASAN AND CICI_OS_MACOS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(STATUS "CICI_ENABLE_ASAN=ON: enabling macOS AddressSanitizer for Debug and disabling IPO")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)
    add_compile_options(
        "$<$<AND:$<CONFIG:Debug>,$<COMPILE_LANGUAGE:C>>:-fsanitize=address>"
        "$<$<AND:$<CONFIG:Debug>,$<COMPILE_LANGUAGE:CXX>>:-fsanitize=address>"
        "$<$<AND:$<CONFIG:Debug>,$<COMPILE_LANGUAGE:OBJC>>:-fsanitize=address>"
        "$<$<AND:$<CONFIG:Debug>,$<COMPILE_LANGUAGE:OBJCXX>>:-fsanitize=address>"
        "$<$<AND:$<CONFIG:Debug>,$<COMPILE_LANGUAGE:C>>:-fno-omit-frame-pointer>"
        "$<$<AND:$<CONFIG:Debug>,$<COMPILE_LANGUAGE:CXX>>:-fno-omit-frame-pointer>"
        "$<$<AND:$<CONFIG:Debug>,$<COMPILE_LANGUAGE:OBJC>>:-fno-omit-frame-pointer>"
        "$<$<AND:$<CONFIG:Debug>,$<COMPILE_LANGUAGE:OBJCXX>>:-fno-omit-frame-pointer>"
    )
    add_link_options("$<$<CONFIG:Debug>:-fsanitize=address>")
    add_compile_definitions("$<$<CONFIG:Debug>:CICI_ENABLE_ASAN=1>")
    if(CICI_GEN_XCODE)
        set(CMAKE_XCODE_SCHEME_ADDRESS_SANITIZER ON)
        set(CMAKE_XCODE_SCHEME_ADDRESS_SANITIZER_USE_AFTER_RETURN ON)
    endif()
elseif(CICI_ENABLE_ASAN)
    message(WARNING "CICI_ENABLE_ASAN is supported only for MSVC and macOS Clang Debug builds")
endif()

# Enable C17 / C++20 globally.
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# .mm files are compiled as OBJCXX; keep its standard aligned with CXX,
# otherwise char8_t/std::u8string in shared PCH may fail to compile.
if(CICI_OS_MACOS)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    set(CMAKE_OBJCXX_EXTENSIONS OFF)
    if(CICI_GEN_XCODE)
        # Xcode links the Objective-C runtime automatically for targets that
        # contain Objective-C++ sources. Do not add CMake's detected implicit
        # objc library a second time.
        list(REMOVE_ITEM CMAKE_OBJCXX_IMPLICIT_LINK_LIBRARIES objc)
    endif()
endif()

# 1. Create an abstract global configuration target.
add_library(ciciglobal_pch INTERFACE)
target_include_directories(ciciglobal_pch INTERFACE
    "${CICI_PUBLIC_SOURCE_DIR}/3rd/include"
    "${CICI_PUBLIC_SOURCE_DIR}/cicibase/include"
)

# 2. Attach the shared foundation header.
target_precompile_headers(ciciglobal_pch INTERFACE 
    "$<$<COMPILE_LANGUAGE:CXX>:${CICI_PUBLIC_SOURCE_DIR}/cicibase/include/ciciglobal_pch.h>"
    # Objective-C++ (.mm) files.
    "$<$<COMPILE_LANGUAGE:OBJCXX>:${CICI_PUBLIC_SOURCE_DIR}/cicibase/include/ciciglobal_pch.h>"
)

if(NOT DEFINED CICI_CHANNEL_ID OR "${CICI_CHANNEL_ID}" STREQUAL "")
    if(DEFINED ENV{CICI_CHANNEL_ID} AND NOT "$ENV{CICI_CHANNEL_ID}" STREQUAL "")
        set(CICI_CHANNEL_ID "$ENV{CICI_CHANNEL_ID}")
    else()
        set(CICI_CHANNEL_ID "")
    endif()
endif()

# 2. Configure generated headers. Use @ONLY to avoid touching script-style placeholders.
configure_file(
    "${CICI_PUBLIC_SOURCE_DIR}/cicibase/ciciconfigure.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/ciciconfigure.h"
    @ONLY
)

include_directories("${CMAKE_CURRENT_BINARY_DIR}/generated")

# Trim absolute source paths so __FILE__ resolves to project-relative paths.
if(CICI_COMPILER_CLANGCL)
  add_compile_options(/GR-)
  add_compile_options("/utf-8")
  add_compile_options("/clang:-ffile-prefix-map=${CMAKE_SOURCE_DIR}/=")
  if(NOT CICI_PUBLIC_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    add_compile_options("/clang:-ffile-prefix-map=${CICI_PUBLIC_SOURCE_DIR}/=public/")
  endif()
  add_compile_definitions(_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING)
elseif(CICI_COMPILER_MSVC)
  add_compile_options(/wd5049)
  add_compile_options(/GR-)
  add_compile_options("/utf-8")
  # 1. Sanitize __FILE__ paths globally.
  # Use /d1trimfile to remove the source root from emitted file paths.
  # Add it to the global C++ compiler flags.
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /d1trimfile:\"${CMAKE_SOURCE_DIR}/\"")

  # 2. PDB path sanitization is configured per target/configuration:
  # Debug keeps full paths so local debugging and symbol loading remain stable.
  # Non-Debug can enable /PDBALTPATH:%_PDB% for path sanitization.
    
  # Apply the same trimming to C files as well.
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /d1trimfile:\"${CMAKE_SOURCE_DIR}/\"")
  add_compile_definitions(_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING)
elseif(CICI_OS_MACOS)
  add_compile_options(-fno-rtti)
  if(CICI_GEN_XCODE)
    set(CMAKE_XCODE_ATTRIBUTE_GCC_ENABLE_CPP_RTTI "NO")
    #set(CMAKE_XCODE_ATTRIBUTE_GCC_ENABLE_CPP_EXCEPTIONS "NO")
  endif()
  add_compile_options("-fmacro-prefix-map=${CMAKE_SOURCE_DIR}/=")
  if(NOT CICI_PUBLIC_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    add_compile_options("-fmacro-prefix-map=${CICI_PUBLIC_SOURCE_DIR}/=public/")
  endif()
  set(CMAKE_CXX_VISIBILITY_PRESET hidden)   #eliminate useless symbols
  set(CMAKE_C_VISIBILITY_PRESET hidden)   #eliminate useless symbols
  set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
endif()

if(CICI_OS_MACOS AND OFFICIAL_BUILD)
    if(CICI_PUBLIC_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
        set(_cici_source_role "public")
    else()
        set(_cici_source_role "runtime")
    endif()
    # dsymutil still uses real object paths from the linker debug map.
    add_compile_options(
        "-fdebug-prefix-map=${CMAKE_SOURCE_DIR}=/CiciSources/${_cici_source_role}"
        "-fdebug-prefix-map=${CICI_PUBLIC_SOURCE_DIR}=/CiciSources/public"
        "-fdebug-prefix-map=${CMAKE_BINARY_DIR}=/CiciBuild/${_cici_source_role}"
        "-fdebug-compilation-dir=/CiciBuild/${_cici_source_role}"
    )
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT CICI_COMPILER_CLANGCL)
  add_compile_options(-finput-charset=UTF-8)
  add_compile_options(-fexec-charset=UTF-8)
endif()

if(MSVC)
  add_compile_options("$<$<COMPILE_LANGUAGE:C>:/utf-8>")
  add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/utf-8>")
endif()


if (CICI_OS_MACOS)
# Set the minimum supported macOS version.
set(CMAKE_OSX_DEPLOYMENT_TARGET "15" CACHE STRING "Minimum macOS version" FORCE)
endif()

# ------------------------------------------------------------------------------
# Debug symbol policy, applied to every configuration.
# 1) STATIC_LIBRARY: debug information is embedded into .lib/.a files.
# 2) SHARED/MODULE/EXECUTABLE: debug information is emitted separately
#    (Windows: .pdb, macOS: .dSYM).
# ------------------------------------------------------------------------------
if(CICI_OS_MACOS)
    find_program(CICI_DSYMUTIL_EXECUTABLE dsymutil)
    if(NOT CICI_DSYMUTIL_EXECUTABLE)
        message(WARNING "dsymutil not found: non-Xcode builds cannot emit standalone dSYM files")
    endif()
endif()

function(cici_apply_debug_symbol_policy target_name)
    if(NOT TARGET ${target_name})
        return()
    endif()

    get_target_property(_is_imported ${target_name} IMPORTED)
    if(_is_imported)
        return()
    endif()

    get_target_property(_target_type ${target_name} TYPE)
    if(NOT _target_type)
        return()
    endif()

    set(_is_static FALSE)
    if(_target_type STREQUAL "STATIC_LIBRARY")
        set(_is_static TRUE)
    endif()

    set(_is_binary FALSE)
    if(_target_type STREQUAL "SHARED_LIBRARY"
        OR _target_type STREQUAL "MODULE_LIBRARY"
        OR _target_type STREQUAL "EXECUTABLE")
        set(_is_binary TRUE)
    endif()

    if(NOT _is_static AND NOT _is_binary)
        return()
    endif()

    if(CICI_COMPILER_MSVC)
        if(_is_static)
            # /Z7 writes debug information into .obj files, so static libraries carry it.
            target_compile_options(${target_name} PRIVATE /Z7)
        elseif(_is_binary)
            # /Zi + /DEBUG generates standalone PDB files for all configurations.
            target_compile_options(${target_name} PRIVATE /Zi)
            target_link_options(${target_name} PRIVATE /DEBUG)

            # Keep compiler PDBs with disposable build intermediates. Final linker
            # PDBs use a stable sanitized absolute path so Visual Studio can locate
            # them without embedding a developer workspace path in release binaries.
            set_target_properties(${target_name} PROPERTIES
                COMPILE_PDB_NAME "${target_name}"
                COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/compile-pdb"
                PDB_NAME "${target_name}"
                PDB_OUTPUT_DIRECTORY "${CICI_PDB_OUTPUT_ROOT}"
            )
            if(CMAKE_CONFIGURATION_TYPES)
                foreach(_cfg ${CMAKE_CONFIGURATION_TYPES})
                    string(TOUPPER "${_cfg}" _cfg_upper)
                    set_target_properties(${target_name} PROPERTIES
                        "COMPILE_PDB_OUTPUT_DIRECTORY_${_cfg_upper}" "${CMAKE_BINARY_DIR}/compile-pdb/${_cfg}"
                        "PDB_OUTPUT_DIRECTORY_${_cfg_upper}" "${CICI_PDB_OUTPUT_ROOT}/${_cfg}"
                    )
                endforeach()
            elseif(CMAKE_BUILD_TYPE)
                string(TOUPPER "${CMAKE_BUILD_TYPE}" _cfg_upper)
                set_target_properties(${target_name} PROPERTIES
                    "COMPILE_PDB_OUTPUT_DIRECTORY_${_cfg_upper}" "${CMAKE_BINARY_DIR}/compile-pdb/${CMAKE_BUILD_TYPE}"
                    "PDB_OUTPUT_DIRECTORY_${_cfg_upper}" "${CICI_PDB_OUTPUT_ROOT}/${CMAKE_BUILD_TYPE}"
                )
            endif()
        endif()
    elseif(CICI_OS_MACOS)
        # Apply -g to every configuration so static libraries embed debug info and
        # dynamic targets can emit separate dSYM bundles.
        target_compile_options(${target_name} PRIVATE -g)

        if(_is_binary)
            set(_symbol_output_dir "${CICI_RUNTIME_OUTPUT_ROOT}/$<CONFIG>")
            if(NOT CMAKE_CONFIGURATION_TYPES)
                set(_symbol_output_dir "${CICI_RUNTIME_OUTPUT_ROOT}")
            endif()
            if(CICI_GEN_XCODE)
                set_target_properties(${target_name} PROPERTIES
                    XCODE_ATTRIBUTE_GCC_GENERATE_DEBUGGING_SYMBOLS "YES"
                    XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym"
                    # Keep dSYM output aligned with the target output directory.
                    XCODE_ATTRIBUTE_DWARF_DSYM_FOLDER_PATH "${CICI_RUNTIME_OUTPUT_ROOT}/$(CONFIGURATION)"
                )
            elseif(CICI_DSYMUTIL_EXECUTABLE)
                set(_symbol_bundle "${_symbol_output_dir}/$<TARGET_FILE_NAME:${target_name}>.dSYM")
                set(_strip_helper "${CMAKE_SOURCE_DIR}/cmake/strip_if_release.cmake")
                add_custom_command(TARGET ${target_name} POST_BUILD
                    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_symbol_output_dir}"
                    COMMAND "${CICI_DSYMUTIL_EXECUTABLE}" "$<TARGET_FILE:${target_name}>" -o "${_symbol_bundle}"
                    # Strip only in Release/RelWithDebInfo. Keep Debug symbols for local debugging.
                    COMMAND "${CMAKE_COMMAND}"
                        "-DCICI_STRIP_TOOL=${CMAKE_STRIP}"
                        "-DCICI_TARGET_BINARY=$<TARGET_FILE:${target_name}>"
                        "-DCICI_BUILD_CONFIG=$<CONFIG>"
                        -P "${_strip_helper}"
                    COMMENT "Generate standalone dSYM for ${target_name} (strip in Release/RelWithDebInfo only)"
                    VERBATIM
                )
            endif()
        endif()
    endif()
endfunction()

