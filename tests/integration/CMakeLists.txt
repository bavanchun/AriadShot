# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

find_package(PkgConfig REQUIRED)
pkg_check_modules(WaylandClient REQUIRED IMPORTED_TARGET wayland-client)

ariadshot_add_test(NAME wayland.protocol-inventory
    SOURCES wayland/tst_ProtocolInventory.cpp
    LIBS PkgConfig::WaylandClient
    LABELS wayland)
