# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

# Qt's plugin-import expression around a target that is not a Qt plugin. The check cannot attribute it to a Qt module,
# so configuring must fail closed.
cmake_minimum_required(VERSION 3.28)
project(arch_qt_plugin_not_a_plugin LANGUAGES CXX)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui)
include("${ARIADSHOT_CMAKE_DIR}/AriadShotArchitecture.cmake")

__qt_internal_get_static_plugin_condition_genex(QOffscreenIntegrationPlugin condition)
ariadshot_add_module(NAME app KIND STATIC ALLOWED Qt6::Gui)
target_sources(ariadshot_app PRIVATE Fixture.cpp)
target_link_libraries(ariadshot_app PUBLIC Qt6::Gui PRIVATE "$<${condition}:Qt6::Gui>")
