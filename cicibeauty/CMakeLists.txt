# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: GPL-2.0-or-later
# See LICENSES/GPL-2.0-or-later.txt for the full license text.

set(TargetName "cicibeauty")

set(main_FILES
        lumaclient.h
        lumaclient.cpp
        videoclient.h
        videoclient.cpp
        obsinfo.h
        obsinfo.cpp
        obsutility.h
        obsutility.cpp
        obscallbackthreadlogger.h
        obscallbackthreadlogger.cpp
        modelapp.h
        effectcatalog.h
        effectcatalog.cpp
        settingconfig.h
        settingconfig.cpp
        cicibeautyapp.h
        cicibeautyapp.cpp
        cicibeautyfilterinstance.h
        cicibeautyfilterinstance.cpp
        i18nsupport.h
        i18nsupport.cpp
        qstringconv.h)

set(statistics_FILES
        statistics/cicireport.h
        statistics/cicireport.cpp
        statistics/eventid.h
        statistics/loginanalytics.h
        statistics/loginanalytics.cpp
        statistics/updateanalytics.h
        statistics/updateanalytics.cpp
        statistics/settingsanalytics.h
        statistics/settingsanalytics.cpp
        statistics/settingspageanalytics.h
        statistics/settingspageanalytics.cpp
        statistics/feedbackanalytics.h
        statistics/feedbackanalytics.cpp
        statistics/subscriptionanalytics.h
        statistics/subscriptionanalytics.cpp
)
        
SOURCE_GROUP(statistics FILES ${statistics_FILES})

set(login_FILES
        login/loginlogic.h
        login/loginlogic.cpp)

SOURCE_GROUP(login FILES ${login_FILES})

set(subscription_FILES
        subscription/subscriptioncontroller.h
        subscription/subscriptioncontroller.cpp)

SOURCE_GROUP(subscription FILES ${subscription_FILES})

set(update_FILES
        update/updatedescriptionpages.h
        update/updatedescriptionpages.cpp
        update/updateui.h
        update/updateui.cpp
        update/updateclient.h
        update/updateclient.cpp)

SOURCE_GROUP(update FILES ${update_FILES})

set(feedback_FILES
        feedback/feedbackclient.h
        feedback/feedbackclient.cpp
        feedback/feedbackdialog.h
        feedback/feedbackdialog.cpp)

SOURCE_GROUP(feedback FILES ${feedback_FILES})

set(uicontroller_FILES
        ui/contextbarentrybutton.h
        ui/contextbarentrybutton.cpp
        ui/contextbarentrycontroller.h
        ui/contextbarentrycontroller.cpp
        ui/sourcelistcicistatuscontroller.h
        ui/sourcelistcicistatuscontroller.cpp
        ui/loginwidget.h
        ui/loginwidget.cpp
        ui/ciciwidget.h
        ui/ciciwidget.cpp
        ui/avatarimagewidget.h
        ui/avatarimagewidget.cpp
        ui/accountentrywidget.h
        ui/accountentrywidget.cpp
        ui/settingaccessoverlay.h
        ui/settingaccessoverlay.cpp
        ui/subscriptionui.h
        ui/subscriptionui.cpp
        ui/subscriptionuiformatter.h
        ui/subscriptionuiformatter.cpp
        ui/dynamicparamui.h
        ui/dynamicparamui.cpp
        ui/commonui.h
        ui/commonui.cpp
        ui/solutiontabhelper.h
        ui/assetsmgr.h
        ui/assetsmgr.cpp
        ui/aieffectcontroller.h
        ui/aieffectcontroller.cpp
        ui/gpurenderwidget.h
        ui/gpurenderwidget.mm
        ui/keyingcolorpickwidget.h
        ui/keyingcolorpickwidget.cpp
        ui/nokeyingzonewidget.h
        ui/nokeyingzonewidget.cpp
        ui/legwidget.h
        ui/legwidget.cpp
        ui/imagetextwidget.h
        ui/imagetextwidget.cpp
        ui/effectitemwidget.h
        ui/effectitemwidget.cpp
        ui/marqueebutton.h
        ui/marqueebutton.cpp
        ui/marqueelabel.h
        ui/marqueelabel.cpp
        ui/gridmorelayout.h
        ui/gridmorelayout.cpp
        ui/basicsettingcontroller.h
        ui/basicsettingcontroller.cpp
        ui/beautyenhancementcontroller.h
        ui/beautyenhancementcontroller.cpp
        ui/bodyshapingsettingcontroller.h
        ui/bodyshapingsettingcontroller.cpp
        ui/filtersettingcontroller.h
        ui/filtersettingcontroller.cpp
        ui/importexportcontroller.h
        ui/importexportcontroller.cpp
        ui/keyingsettingcontroller.h
        ui/keyingsettingcontroller.cpp
        ui/lenseffectscontroller.h
        ui/lenseffectscontroller.cpp
        ui/makeupsettingcontroller.h
        ui/makeupsettingcontroller.cpp
        ui/settingcontroller.h
        ui/settingcontroller.cpp
        ui/specialeffectsassetscontroller.h
        ui/specialeffectsassetscontroller.cpp)

if(CICI_OS_WINDOWS)
    set_source_files_properties(ui/gpurenderwidget.mm PROPERTIES LANGUAGE CXX)
endif()
        
SOURCE_GROUP(ui FILES ${uicontroller_FILES})

set(effect_FILES
        effect/effectcontroller.h
        effect/effectcontroller.cpp)
                
SOURCE_GROUP(effect FILES ${effect_FILES})

set(TS_FILES
        ${CICI_PUBLIC_SOURCE_DIR}/cicibeauty/ui/translations/cicibeauty_zh_CN.ts
        ${CICI_PUBLIC_SOURCE_DIR}/cicibeauty/ui/translations/cicibeauty_zh_TW.ts
        ${CICI_PUBLIC_SOURCE_DIR}/cicibeauty/ui/translations/cicibeauty_en_US.ts)

set(I18N_SOURCE_FILES
        ${main_FILES}
        ${update_FILES}
        ${feedback_FILES}
        ${subscription_FILES}
        ${login_FILES}
        ${uicontroller_FILES})

set(BUILD_SHARED_LIBS ON)
# Used only to provide headers and moc/uic/rcc tooling; Qt libraries are not linked here.
find_package(Qt6 COMPONENTS Core Widgets Gui)
if(NOT Qt6_FOUND)
    find_package(Qt5 5.15 COMPONENTS Core Widgets Gui REQUIRED)
endif()

# Generate/update .ts files with lupdate. This target is intentionally manual:
# normal builds should consume existing translations, not rewrite source files.
add_custom_target(UpdateTranslations
    COMMAND ${LUPDATE_PATH} ${I18N_SOURCE_FILES} -ts ${TS_FILES}
    DEPENDS ${I18N_SOURCE_FILES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Updating translation source files (.ts)"
    VERBATIM
)

# Compile .ts files into .qm files with lrelease.
set(QM_FILES)
foreach(TS_FILE ${TS_FILES})
    get_filename_component(QM_FILE ${TS_FILE} NAME_WE)
    set(QM_FILE "${CICI_PUBLIC_SOURCE_DIR}/cicibeauty/ui/translations/${QM_FILE}.qm")
    list(APPEND QM_FILES ${QM_FILE})
    add_custom_command(
        OUTPUT ${QM_FILE}
#       COMMAND ${Qt6_LRELEASE_EXECUTABLE} ${TS_FILE} -qm ${QM_FILE}
        COMMAND ${LRELEASE_PATH} ${TS_FILE} -qm ${QM_FILE}
        DEPENDS ${TS_FILE}
        COMMENT "Compiling ${TS_FILE} to ${QM_FILE}"
)
endforeach()

# Process resource files.
# Resource configuration used to embed QM files into the binary.
set(RCC_RESOURCES
            ${CICI_PUBLIC_SOURCE_DIR}/cicibeauty/ui/translations/translations.qrc
)

set(RESOURCE_FILES ${CMAKE_CURRENT_BINARY_DIR}/qrc_translations.cpp)
add_custom_command(
    OUTPUT ${RESOURCE_FILES}
    COMMAND ${RCC_PATH} ${RCC_RESOURCES} -o ${RESOURCE_FILES}
    DEPENDS ${RCC_RESOURCES} ${QM_FILES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Compiling qrc"
)

# Create a target that triggers translation processing.
add_custom_target(Translations ALL DEPENDS ${QM_FILES})

# Convert xxx.ui files into ui_xxx.h with uic.
set(UIHEADER_FILES)
foreach(UI_FILE ${UI_FILES})
    get_filename_component(HEADER_FILE ${UI_FILE} NAME_WE)
    set(HEADER_FILE "${CICI_PUBLIC_SOURCE_DIR}/cicibeauty/ui/ui_${HEADER_FILE}.h")
    list(APPEND UIHEADER_FILES ${HEADER_FILE})
    add_custom_command(
        OUTPUT ${HEADER_FILE}
        COMMAND ${UIC_PATH} ${UI_FILE} -o ${HEADER_FILE}
        DEPENDS ${UI_FILE}
        WORKING_DIRECTORY ${CICI_PUBLIC_SOURCE_DIR}/cicibeauty
        COMMENT "convert ${UI_FILE} to ${HEADER_FILE}"
    )
endforeach()
add_custom_target(ui2header ALL DEPENDS ${UIHEADER_FILES})

# Enable Qt automatic processing (MOC/UIC/RCC).
set(CMAKE_AUTOMOC ON)
#set(CMAKE_AUTOUIC ON)  # Already handled manually above.
#set(CMAKE_AUTORCC ON)  # Already handled manually above.
if(CICI_OS_MACOS)
    # 1. Disable CMake-generated local build paths to avoid embedding /Users/xxx/...
    set(CMAKE_SKIP_BUILD_RPATH FALSE)
    set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE)

    # 2. Define the exact RPATH list required by the package layout.
    set(MY_RPATHS 
        "@loader_path" 
        "@loader_path/../Frameworks" 
        "@executable_path/../Frameworks"
    )

    # 3. Apply the list to the install RPATH.
    set(CMAKE_INSTALL_RPATH "${MY_RPATHS}")

    # 4. Ensure targets use RPATH.
    set(CMAKE_MACOSX_RPATH TRUE)
endif()

qt_add_library(${TargetName} MODULE
                    ${main_FILES}
                    ${statistics_FILES}
                    ${update_FILES}
                    ${feedback_FILES}
                    ${subscription_FILES}
                    ${uicontroller_FILES}
                    ${effect_FILES}
                    ${login_FILES}
#                   ${payment_FILES}
                    ${QM_FILES}     # Compiled translation files.
                    ${RESOURCE_FILES} # Qt resource files.
    )

cici_apply_debug_symbol_policy(${TargetName})

target_precompile_headers(${TargetName} PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/qstringconv.h>"
    #"$<$<COMPILE_LANGUAGE:C>:${CMAKE_CURRENT_SOURCE_DIR}/ciciglobal_pch.h>"
    # Objective-C++ (.mm) files.
    "$<$<COMPILE_LANGUAGE:OBJCXX>:${CMAKE_CURRENT_SOURCE_DIR}/qstringconv.h>"
)

if (CICI_OS_MACOS)
if(CICI_GEN_XCODE)
if(OFFICIAL_BUILD)
    set_target_properties(${TargetName} PROPERTIES
        XCODE_ATTRIBUTE_DEPLOYMENT_POSTPROCESSING "YES"
        XCODE_ATTRIBUTE_STRIP_INSTALLED_PRODUCT "YES"
        XCODE_ATTRIBUTE_STRIP_STYLE "non-global"
    )
else()
    set_target_properties(${TargetName} PROPERTIES
        XCODE_ATTRIBUTE_DEPLOYMENT_POSTPROCESSING "NO"
        XCODE_ATTRIBUTE_STRIP_INSTALLED_PRODUCT "NO"
    )
endif()
endif()
set(EXPORT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/exported_symbols.exp")
    # Tell the linker to use this exported symbol allowlist.
target_link_options(${TargetName} PRIVATE "-Wl,-exported_symbols_list,${EXPORT_FILE}")

if(CICI_GEN_XCODE)
set_target_properties(${TargetName} PROPERTIES
    XCODE_SCHEME_ENVIRONMENT "DYLD_LIBRARY_PATH=$(TARGET_BUILD_DIR)"
)
endif()
elseif(CICI_OS_WINDOWS)
target_compile_options(${TargetName} PRIVATE /GR)
else()
message(FATAL_ERROR "Unsupported platform: cicibeauty supports only Windows and macOS")
endif()
                    
# Connect the translation target to the main target.
add_dependencies(${TargetName} Translations)
# Connect the ui2header target to the main target.
add_dependencies(${TargetName} ui2header)


# Project and third-party include directories.
target_include_directories(${TargetName} PRIVATE ${libobs_INCLUDE_DIRS})
target_include_directories(${TargetName} PRIVATE ${frontendapi_INCLUDE_DIRS})
target_include_directories(${TargetName} PRIVATE ${3RD_DIR}/include)
target_include_directories(${TargetName} PRIVATE ${3RD_DIR}/spdlog/include)

if (CICI_OS_MACOS)
target_include_directories(${TargetName} PRIVATE ${3RD_DIR}/include/macos)
elseif(CICI_OS_WINDOWS)
target_include_directories(${TargetName} PRIVATE ${3RD_DIR}/include/windows)
else()
message(FATAL_ERROR "Unsupported platform: cicibeauty supports only Windows and macOS")
endif()

target_link_directories(${TargetName} PRIVATE ${CICI_ARCHIVE_LINK_DIRECTORY})

# Third-party library directories.
if(CICI_OS_WINDOWS)
target_link_directories(${TargetName} PRIVATE ${3RD_DIR}/libs/windows)
target_link_directories(${TargetName} PRIVATE
    ${CICI_OBS_LIBRARY_DIRS}
)
endif()

target_link_libraries(${TargetName} PRIVATE cicibase ciciprotocol)
if (CICI_OS_MACOS)
    target_link_libraries(${TargetName} PRIVATE
        $<$<CONFIG:Debug>:${CICI_OBS_FRONTEND_LIB_DIRS}/Debug/obs-frontend-api.dylib>
        $<$<CONFIG:Release>:${CICI_OBS_FRONTEND_LIB_DIRS}/Release/obs-frontend-api.dylib>
        $<$<CONFIG:RelWithDebInfo>:${CICI_OBS_FRONTEND_LIB_DIRS}/Release/obs-frontend-api.dylib>
    )

    target_link_libraries(${TargetName} PRIVATE
        $<$<CONFIG:Debug>:${libobs_LIB_DIRS}/Debug/libobs.framework>
        $<$<CONFIG:Release>:${libobs_LIB_DIRS}/Release/libobs.framework>
        $<$<CONFIG:RelWithDebInfo>:${libobs_LIB_DIRS}/Release/libobs.framework>
    )
    target_link_libraries(${TargetName} PRIVATE "-framework Cocoa")
    target_link_libraries(${TargetName} PRIVATE "-framework QuartzCore")
    target_link_libraries(${TargetName} PRIVATE "-framework CoreVideo")
    target_link_libraries(${TargetName} PRIVATE "-framework OpenGL")
    target_link_libraries(${TargetName} PRIVATE "-framework AVFoundation")
    target_link_libraries(${TargetName} PRIVATE "-framework CoreMedia")
    target_link_libraries(${TargetName} PRIVATE "-framework Accelerate")
    target_link_libraries(${TargetName} PRIVATE "-framework CoreML")
    target_link_libraries(${TargetName} PRIVATE "-framework Security")
    target_link_libraries(${TargetName} PRIVATE "-framework Metal")
elseif(CICI_OS_WINDOWS)
    target_link_libraries(${TargetName} PRIVATE obs)
    target_link_libraries(${TargetName} PRIVATE obs-frontend-api)
    target_link_libraries(${TargetName} PRIVATE shlwapi.lib)
else()
message(FATAL_ERROR "Unsupported platform: cicibeauty supports only Windows and macOS")
endif()

# Add Qt include directories only where possible to keep runtime behavior tied to the host process Qt.
if(Qt6_FOUND)
    target_include_directories(${TargetName} PRIVATE
        $<TARGET_PROPERTY:Qt6::Core,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:Qt6::Gui,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:Qt6::Widgets,INTERFACE_INCLUDE_DIRECTORIES>
    )
    target_link_libraries(${TargetName} PRIVATE Qt6::Core Qt6::Widgets Qt6::Gui)
else()
    target_include_directories(${TargetName} PRIVATE
        $<TARGET_PROPERTY:Qt5::Core,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:Qt5::Gui,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:Qt5::Widgets,INTERFACE_INCLUDE_DIRECTORIES>
    )
    target_link_libraries(${TargetName} PRIVATE Qt5::Core Qt5::Widgets Qt5::Gui)
endif()

message(STATUS "Qt6 install path: ${Qt6_DIR}")
message(STATUS "Qt6 version: ${Qt6_VERSION}")
message(STATUS "Qt6 Widgets found: ${Qt6Widgets_FOUND}")

# Clean RPATH to avoid embedding any Qt paths.
#set_target_properties(cicibeauty PROPERTIES
#    BUILD_RPATH ""
#    INSTALL_RPATH ""
#    SKIP_BUILD_RPATH TRUE
#    MACOSX_RPATH OFF
#)

if (CICI_OS_MACOS)
set_target_properties(${TargetName} PROPERTIES
    PREFIX ""
    SUFFIX ".so"
)
elseif(CICI_OS_WINDOWS)
set_target_properties(${TargetName} PROPERTIES
    PREFIX ""
    SUFFIX ".dll"
)
else()
message(FATAL_ERROR "Unsupported platform: cicibeauty supports only Windows and macOS")
endif()

set_target_properties(${TargetName} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CICI_RUNTIME_OUTPUT_ROOT})

if (CICI_OS_MACOS AND CICI_GEN_XCODE)
# For Xcode builds, use local signing when needed.
set_target_properties(${TargetName} PROPERTIES CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development")
endif()

if(CICI_BUILD_TESTS)
    if(CICI_OS_MACOS)
        # Integration test: real libobs sources/signals; frontend selection is supplied by the fixture.
        add_executable(obsinfo_tests
            tests/obsinfo_tests.cpp
            obsinfo.cpp
            obscallbackthreadlogger.cpp
        )
        target_include_directories(obsinfo_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${libobs_INCLUDE_DIRS}
        )
        target_link_libraries(obsinfo_tests PRIVATE cicibase ciciglobal_pch
            "$<$<CONFIG:Debug>:${libobs_LIB_DIRS}/Debug/libobs.framework>"
            "$<$<NOT:$<CONFIG:Debug>>:${libobs_LIB_DIRS}/Release/libobs.framework>"
            "-framework CoreFoundation"
        )
        if(Qt6_FOUND)
            target_link_libraries(obsinfo_tests PRIVATE Qt6::Core)
        else()
            target_link_libraries(obsinfo_tests PRIVATE Qt5::Core)
        endif()
        set_target_properties(obsinfo_tests PROPERTIES BUILD_WITH_INSTALL_RPATH FALSE)
        cici_apply_debug_symbol_policy(obsinfo_tests)
        add_test(NAME obsinfo_tests COMMAND obsinfo_tests)
        # libobs's runtime dependencies are supplied by an installed OBS, not the plugin SDK.
        set_tests_properties(obsinfo_tests PROPERTIES TIMEOUT 30
            ENVIRONMENT "CICI_OBS_RUNTIME_DIR=${CICI_OBS_RUNTIME_DIR};DYLD_FRAMEWORK_PATH=${QT_PATH}/lib:${CICI_OBS_RUNTIME_DIR};DYLD_LIBRARY_PATH=${CICI_OBS_RUNTIME_DIR}"
        )
    endif()

    add_executable(i18nsupport_tests
        tests/i18nsupport_tests.cpp
        i18nsupport.cpp
        i18nsupport.h
    )
    target_include_directories(i18nsupport_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${3RD_DIR}/include
    )
    if(Qt6_FOUND)
        target_link_libraries(i18nsupport_tests PRIVATE Qt6::Core)
    else()
        target_link_libraries(i18nsupport_tests PRIVATE Qt5::Core)
    endif()
    cici_apply_debug_symbol_policy(i18nsupport_tests)
    target_link_libraries(i18nsupport_tests PRIVATE ciciglobal_pch)
    add_test(NAME i18nsupport_tests COMMAND i18nsupport_tests)
    set_tests_properties(i18nsupport_tests PROPERTIES
        TIMEOUT 30
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${QT_PATH}/bin"
    )
    if(CICI_OS_MACOS)
        set_tests_properties(i18nsupport_tests PROPERTIES ENVIRONMENT
            "DYLD_FRAMEWORK_PATH=${QT_PATH}/lib")
    endif()

endif()
