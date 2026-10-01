# SPDX-FileCopyrightText: 2026 Andy Stewart
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.21)
project(omarchy_screenshot VERSION 0.1.6 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick Test)
find_package(LayerShellQt REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

set(VIRTUAL_POINTER_XML ${CMAKE_CURRENT_SOURCE_DIR}/protocols/wlr-virtual-pointer-unstable-v1.xml)
set(VIRTUAL_POINTER_HEADER ${CMAKE_CURRENT_BINARY_DIR}/wlr-virtual-pointer-client-protocol.h)
set(VIRTUAL_POINTER_CODE ${CMAKE_CURRENT_BINARY_DIR}/wlr-virtual-pointer-protocol.c)
add_custom_command(
    OUTPUT ${VIRTUAL_POINTER_HEADER} ${VIRTUAL_POINTER_CODE}
    COMMAND ${WAYLAND_SCANNER} client-header ${VIRTUAL_POINTER_XML} ${VIRTUAL_POINTER_HEADER}
    COMMAND ${WAYLAND_SCANNER} private-code ${VIRTUAL_POINTER_XML} ${VIRTUAL_POINTER_CODE}
    DEPENDS ${VIRTUAL_POINTER_XML}
)
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

qt_add_executable(omarchy-screenshot
    src/main.cpp
    src/capturecontroller.cpp
    src/capturecontroller.h
    src/scrollstitcher.cpp
    src/scrollstitcher.h
    src/longimageitem.cpp
    src/longimageitem.h
    src/virtualpointer.cpp
    src/virtualpointer.h
    ${VIRTUAL_POINTER_CODE}
    ${VIRTUAL_POINTER_HEADER}
)

set_source_files_properties(qml/Overlay.qml PROPERTIES QT_RESOURCE_ALIAS Overlay.qml)
set_source_files_properties(qml/ToolbarGlyph.qml PROPERTIES QT_RESOURCE_ALIAS ToolbarGlyph.qml)
set_source_files_properties(qml/AnnotationToolbar.qml PROPERTIES QT_RESOURCE_ALIAS AnnotationToolbar.qml)
set_source_files_properties(qml/LongOverlay.qml PROPERTIES QT_RESOURCE_ALIAS LongOverlay.qml)
set_source_files_properties(qml/ScrollCapture.qml PROPERTIES QT_RESOURCE_ALIAS ScrollCapture.qml)
set_source_files_properties(qml/SelectionResizeHandle.qml PROPERTIES QT_RESOURCE_ALIAS SelectionResizeHandle.qml)
qt_add_qml_module(omarchy-screenshot
    URI OmarchyScreenshot
    VERSION 1.0
    QML_FILES qml/Overlay.qml qml/ToolbarGlyph.qml qml/AnnotationToolbar.qml qml/LongOverlay.qml qml/ScrollCapture.qml qml/SelectionResizeHandle.qml
)

target_link_libraries(omarchy-screenshot PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test LayerShellQt::Interface
    PkgConfig::WAYLAND_CLIENT
)
target_include_directories(omarchy-screenshot PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

install(TARGETS omarchy-screenshot RUNTIME DESTINATION bin)
