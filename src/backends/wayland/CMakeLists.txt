# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

# WaylandSession and protocol clients for Linux desktops (docs/spec/02-modules-and-interfaces.md §1, §10;
# docs/spec/04-capture-and-overlay.md §3). Built as STATIC on Linux when wayland-client is available; on other platforms
# or when wayland-client is absent (e.g. macOS or stripped environments), it remains an INTERFACE target.

set(_ariadshot_wayland_protocols
    "${PROJECT_SOURCE_DIR}/third_party/wayland-protocols/wayland-protocols/ext-image-copy-capture-v1.xml"
    "${PROJECT_SOURCE_DIR}/third_party/wayland-protocols/wayland-protocols/ext-image-capture-source-v1.xml"
    "${PROJECT_SOURCE_DIR}/third_party/wayland-protocols/wayland-protocols/ext-data-control-v1.xml"
    "${PROJECT_SOURCE_DIR}/third_party/wayland-protocols/wlr-protocols/wlr-screencopy-unstable-v1.xml"
    "${PROJECT_SOURCE_DIR}/third_party/wayland-protocols/wlr-protocols/wlr-data-control-unstable-v1.xml"
    "${PROJECT_SOURCE_DIR}/third_party/wayland-protocols/wlr-protocols/wlr-virtual-pointer-unstable-v1.xml"
    "${PROJECT_SOURCE_DIR}/third_party/wayland-protocols/wlr-protocols/wlr-layer-shell-unstable-v1.xml"
    "${PROJECT_SOURCE_DIR}/third_party/wayland-protocols/hyprland-protocols/hyprland-toplevel-export-v1.xml"
    "${PROJECT_SOURCE_DIR}/third_party/wayland-protocols/hyprland-protocols/hyprland-global-shortcuts-v1.xml")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WaylandClient IMPORTED_TARGET GLOBAL wayland-client)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND WaylandClient_FOUND)
    ariadshot_add_module(
        NAME backends/wayland
        KIND STATIC
        ALLOWED AriadShot::platform Qt6::Core PkgConfig::WaylandClient)

    target_sources(ariadshot_backends_wayland PRIVATE
        OutputRegistry.h
        OutputRegistry.cpp
        WaylandSession.h
        WaylandSession.cpp)

    ariadshot_generate_wayland_protocols(
        TARGET ariadshot_backends_wayland
        XMLS ${_ariadshot_wayland_protocols})

    target_link_libraries(ariadshot_backends_wayland
        PUBLIC
            AriadShot::platform
            Qt6::Core
        PRIVATE
            PkgConfig::WaylandClient)
else()
    ariadshot_add_module(
        NAME backends/wayland
        KIND INTERFACE
        ALLOWED AriadShot::platform Qt6::Core)
    target_link_libraries(ariadshot_backends_wayland INTERFACE AriadShot::platform)
endif()
