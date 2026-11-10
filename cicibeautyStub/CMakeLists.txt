# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: GPL-2.0-or-later
# See LICENSES/GPL-2.0-or-later.txt for the full license text.

set(TargetName "cicibeautyStub")

set(main_FILES ciciplugin.h stub.cpp)

add_library(${TargetName} MODULE ${main_FILES})

cici_apply_debug_symbol_policy(${TargetName})

if (CICI_OS_MACOS)
set(MACOSX_BUNDLE_EXECUTABLE_NAME "${TargetName}")
set(MACOSX_BUNDLE_BUNDLE_NAME "${TargetName}")
set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.bananapi.${TargetName}")
set_target_properties(${TargetName} PROPERTIES
    # Configure basic bundle metadata.
    BUNDLE TRUE
    BUNDLE_EXTENSION "plugin"
    MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/${TargetName}/Info.plist.in"  # Info.plist path.
    
    # Link-time optimization: strip unused code; debug symbols are handled by the global policy.
    LINK_FLAGS_RELEASE "-dead_strip"
)
endif()

if (CICI_OS_MACOS)
target_compile_definitions(${TargetName} PRIVATE OF_MAC=1)
endif()

target_include_directories(${TargetName} PRIVATE ${libobs_INCLUDE_DIRS})
target_include_directories(${TargetName} PRIVATE ${3RD_DIR}/include)
if (CICI_OS_MACOS)
target_include_directories(${TargetName} PRIVATE ${3RD_DIR}/include/macos)
elseif(CICI_OS_WINDOWS)
target_include_directories(${TargetName} PRIVATE ${3RD_DIR}/include/windows)
else()
message(FATAL_ERROR "Unsupported platform: cicibeautyStub supports only Windows and macOS")
endif()

target_link_libraries(${TargetName} PRIVATE ciciglobal_pch)
if (CICI_OS_MACOS)
target_link_libraries(${TargetName} PRIVATE
  cicibase
	        $<$<CONFIG:Debug>:${libobs_LIB_DIRS}/Debug/libobs.framework>
	        $<$<CONFIG:Release>:${libobs_LIB_DIRS}/Release/libobs.framework>
	        $<$<CONFIG:RelWithDebInfo>:${libobs_LIB_DIRS}/Release/libobs.framework>
	    )
elseif(CICI_OS_WINDOWS)
target_link_directories(${TargetName} PRIVATE
  ${CICI_OBS_LIBRARY_DIRS}
)
target_link_libraries(${TargetName} PRIVATE cicibase obs)
else()
message(FATAL_ERROR "Unsupported platform: cicibeautyStub supports only Windows and macOS")
endif()

#get_filename_component(LIBOBS_FRAMEWORK_DIR "${LIBOBS_FRAMEWORK}" DIRECTORY)
#target_include_directories(${TargetName} PRIVATE "${LIBOBS_FRAMEWORK_DIR}/Headers")

set_target_properties(${TargetName} PROPERTIES PREFIX "")

set_target_properties(${TargetName} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CICI_RUNTIME_OUTPUT_ROOT})

if(CICI_OS_MACOS AND CICI_GEN_XCODE)
  # Use automatic code signing; Xcode will use "Sign to Run Locally".
  set_target_properties(${TargetName} PROPERTIES
    CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.bananapi.${TargetName}"
  )
endif()
