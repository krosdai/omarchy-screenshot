cmake_minimum_required(VERSION 3.21)
project(omarchy_screenshot VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick Concurrent)
find_package(LayerShellQt REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WaylandClient REQUIRED IMPORTED_TARGET wayland-client)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
set(VIRTUAL_POINTER_XML ${CMAKE_CURRENT_SOURCE_DIR}/third_party/omasnap/protocols/wlr-virtual-pointer-unstable-v1.xml)
set(VIRTUAL_POINTER_HEADER ${CMAKE_CURRENT_BINARY_DIR}/wlr-virtual-pointer-unstable-v1-client-protocol.h)
set(VIRTUAL_POINTER_CODE ${CMAKE_CURRENT_BINARY_DIR}/wlr-virtual-pointer-unstable-v1-protocol.c)
add_custom_command(
    OUTPUT ${VIRTUAL_POINTER_HEADER} ${VIRTUAL_POINTER_CODE}
    COMMAND ${WAYLAND_SCANNER} client-header ${VIRTUAL_POINTER_XML} ${VIRTUAL_POINTER_HEADER}
    COMMAND ${WAYLAND_SCANNER} private-code ${VIRTUAL_POINTER_XML} ${VIRTUAL_POINTER_CODE}
    DEPENDS ${VIRTUAL_POINTER_XML}
    VERBATIM
)
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

qt_add_executable(omarchy-screenshot
    src/main.cpp
    src/capturecontroller.cpp
    src/capturecontroller.h
    src/mosaicoverlay.cpp
    src/mosaicoverlay.h
    src/scrollpreview.cpp
    src/scrollpreview.h
    third_party/omasnap/stitch.cpp
    third_party/omasnap/stitch.hpp
    third_party/omasnap/auto-capture.cpp
    third_party/omasnap/auto-capture.hpp
    third_party/omasnap/scroll-inject.cpp
    third_party/omasnap/scroll-inject.hpp
    ${VIRTUAL_POINTER_HEADER}
    ${VIRTUAL_POINTER_CODE}
)
target_include_directories(omarchy-screenshot PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

set_source_files_properties(qml/Overlay.qml PROPERTIES QT_RESOURCE_ALIAS Overlay.qml)
set_source_files_properties(qml/ToolbarGlyph.qml PROPERTIES QT_RESOURCE_ALIAS ToolbarGlyph.qml)
qt_add_qml_module(omarchy-screenshot
    URI OmarchyScreenshot
    VERSION 1.0
    QML_FILES qml/Overlay.qml qml/ToolbarGlyph.qml
)

target_link_libraries(omarchy-screenshot PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Concurrent
    LayerShellQt::Interface PkgConfig::WaylandClient
)

install(TARGETS omarchy-screenshot RUNTIME DESTINATION bin)
install(FILES third_party/omasnap/LICENSE
        DESTINATION share/licenses/omarchy-screenshot
        RENAME Omasnap.txt)
install(FILES third_party/omasnap/protocols/LICENSE
        DESTINATION share/licenses/omarchy-screenshot
        RENAME wlr-virtual-pointer.txt)
