# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

find_package(PkgConfig REQUIRED)
pkg_check_modules(WaylandClient REQUIRED IMPORTED_TARGET wayland-client)

ariadshot_add_test(NAME wayland.protocol-inventory
    SOURCES wayland/tst_ProtocolInventory.cpp
    LIBS PkgConfig::WaylandClient
    LABELS wayland)

if(TARGET ariadshot_backends_wayland)
    get_target_property(_ariadshot_wayland_mod_type ariadshot_backends_wayland TYPE)
    if(_ariadshot_wayland_mod_type STREQUAL "STATIC_LIBRARY")
        ariadshot_add_test(NAME wayland.session
            SOURCES wayland/tst_WaylandSession.cpp
            LIBS AriadShot::backends_wayland PkgConfig::WaylandClient
            LABELS wayland)
    endif()
endif()
