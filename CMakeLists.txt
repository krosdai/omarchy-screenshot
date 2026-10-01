# SPDX-FileCopyrightText: 2026 Andy Stewart
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.21)
project(omarchy_screenshot VERSION 0.1.5 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick Test LinguistTools)
find_package(LayerShellQt REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WaylandClient REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(WaylandProtocols REQUIRED wayland-protocols>=1.37)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

# The image source protocol references foreign toplevels, so its generated
# code needs that interface too.
set(capture_protocols
    staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml
    staging/ext-image-capture-source/ext-image-capture-source-v1.xml
    staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml
)
foreach(protocol IN LISTS capture_protocols)
    get_filename_component(protocol_name "${protocol}" NAME_WE)
    set(protocol_xml "${WAYLAND_PROTOCOLS_DIR}/${protocol}")
    set(protocol_header "${CMAKE_CURRENT_BINARY_DIR}/${protocol_name}-client-protocol.h")
    set(protocol_code "${CMAKE_CURRENT_BINARY_DIR}/${protocol_name}-protocol.c")
    add_custom_command(
        OUTPUT "${protocol_header}" "${protocol_code}"
        COMMAND "${WAYLAND_SCANNER}" client-header "${protocol_xml}" "${protocol_header}"
        COMMAND "${WAYLAND_SCANNER}" private-code "${protocol_xml}" "${protocol_code}"
        DEPENDS "${protocol_xml}"
        VERBATIM
    )
    list(APPEND protocol_sources "${protocol_header}" "${protocol_code}")
endforeach()

qt_add_library(screenshot-core STATIC
    src/capturecontroller.cpp
    src/capturecontroller.h
    src/screencapture.cpp
    src/screencapture.h
    ${protocol_sources}
)
target_include_directories(screenshot-core PUBLIC src PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(screenshot-core PUBLIC Qt6::Core Qt6::Gui PRIVATE PkgConfig::WaylandClient)

qt_add_executable(omarchy-screenshot
    src/main.cpp
)

set_source_files_properties(qml/Overlay.qml PROPERTIES QT_RESOURCE_ALIAS Overlay.qml)
set_source_files_properties(qml/ToolbarGlyph.qml PROPERTIES QT_RESOURCE_ALIAS ToolbarGlyph.qml)
qt_add_qml_module(omarchy-screenshot
    URI OmarchyScreenshot
    VERSION 1.0
    QML_FILES qml/Overlay.qml qml/ToolbarGlyph.qml
)

target_link_libraries(omarchy-screenshot PRIVATE
    screenshot-core Qt6::Qml Qt6::Quick Qt6::Test LayerShellQt::Interface
)

set(screenshot_locales
    az be bg hr cs da nl et fi fr ka de el he hu is ga it ja kk ky lo
    lv lt mk nb pl pt_PT pt_BR ro ru sr sk sl es sv tg tr uk zh_CN zh_TW
)
foreach(locale IN LISTS screenshot_locales)
    list(APPEND screenshot_translations "translations/omarchy-screenshot_${locale}.ts")
endforeach()
qt_add_translations(omarchy-screenshot
    TS_FILES ${screenshot_translations}
    SOURCES src/main.cpp src/capturecontroller.cpp qml/Overlay.qml
    LUPDATE_OPTIONS -no-obsolete -locations none
)

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME translations
        COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tests/translations.py"
            --app $<TARGET_FILE:omarchy-screenshot>
            --lupdate $<TARGET_FILE:Qt6::lupdate>
            --source "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    qt_add_executable(ocr-test tests/ocr.cpp)
    target_link_libraries(ocr-test PRIVATE screenshot-core Qt6::Test)
    add_test(NAME ocr-languages COMMAND ocr-test)
    set_tests_properties(ocr-languages PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
    )
endif()

install(TARGETS omarchy-screenshot RUNTIME DESTINATION bin)
