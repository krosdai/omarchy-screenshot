# SPDX-FileCopyrightText: 2026 Andy Stewart
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.21)
project(omarchy_screenshot VERSION 0.1.8 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick Test LinguistTools)
find_package(LayerShellQt REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols>=1.37)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
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
    src/daemonsocket.cpp
    src/daemonsocket.h
    src/screencapture.cpp
    src/screencapture.h
    ${protocol_sources}
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

set(screenshot_locales
    az be bg hr cs da nl et fi fr ka de el he hu is ga it ja kk ky lo
    lv lt mk nb pl pt_PT pt_BR ro ru sr sk sl es sv tg tr uk zh_CN zh_TW
)
foreach(locale IN LISTS screenshot_locales)
    list(APPEND screenshot_translations "translations/omarchy-screenshot_${locale}.ts")
endforeach()
qt_add_translations(omarchy-screenshot
    TS_FILES ${screenshot_translations}
    SOURCES src/main.cpp src/capturecontroller.cpp src/virtualpointer.cpp qml/Overlay.qml
        qml/AnnotationToolbar.qml qml/LongOverlay.qml qml/ScrollCapture.qml
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
    add_test(NAME scroll-stitch
        COMMAND omarchy-screenshot --scroll-stitch-test)
    set_tests_properties(scroll-stitch PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    # Builds the controller again so reset() is tested without a compositor.
    qt_add_executable(daemon-test
        tests/daemon.cpp
        src/daemonsocket.cpp
        src/capturecontroller.cpp
        src/scrollstitcher.cpp
        src/virtualpointer.cpp
        ${VIRTUAL_POINTER_CODE}
        src/screencapture.cpp
        ${protocol_sources}
    )
    target_include_directories(daemon-test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(daemon-test PRIVATE
        Qt6::Core Qt6::Gui Qt6::Test PkgConfig::WAYLAND_CLIENT)
    add_test(NAME daemon COMMAND daemon-test)
    set_tests_properties(daemon PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1")
endif()

include(GNUInstallDirs)
install(TARGETS omarchy-screenshot RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# systemd starts the resident daemon on the first request and lets it exit
# when idle. Relative to the install prefix, so `--prefix` relocates it; a
# per-user install (~/.local) should point this at ~/.local/share/systemd/user.
set(SYSTEMD_USER_UNIT_DIR "lib/systemd/user"
    CACHE STRING "Where to install the systemd user units")
configure_file(systemd/omarchy-screenshot.service.in
    "${CMAKE_CURRENT_BINARY_DIR}/omarchy-screenshot.service" @ONLY)
install(FILES
    systemd/omarchy-screenshot.socket
    "${CMAKE_CURRENT_BINARY_DIR}/omarchy-screenshot.service"
    DESTINATION "${SYSTEMD_USER_UNIT_DIR}")

include(CTest)
if(BUILD_TESTING)
    qt_add_executable(tst-preview-scaling tests/tst_preview_scaling.cpp)
    target_include_directories(tst-preview-scaling PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(tst-preview-scaling PRIVATE Qt6::Quick Qt6::Test)
    target_compile_definitions(tst-preview-scaling PRIVATE
        PREVIEW_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/preview_scaling.qml")
    foreach(scale IN ITEMS 1 1.25 1.5 2)
        add_test(NAME preview-scaling-${scale} COMMAND tst-preview-scaling)
        set_tests_properties(preview-scaling-${scale} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORMTHEME=;QT_QUICK_BACKEND=software;QT_SCALE_FACTOR=${scale}"
            TIMEOUT 15)
    endforeach()
endif()
