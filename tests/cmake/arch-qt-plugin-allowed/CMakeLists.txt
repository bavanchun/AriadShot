# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

# An app-kind module that links a Qt Gui plugin, and the object library that registers it, in the expression Qt's
# target finalizer adds for static plugins (on Apple, the permission plugins of Qt Core). The condition comes from
# Qt's own helper. Both edges count as Qt6::Gui, which the module may link, so configuring must succeed.
cmake_minimum_required(VERSION 3.28)
project(arch_qt_plugin_allowed LANGUAGES CXX)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui)
include("${ARIADSHOT_CMAKE_DIR}/AriadShotArchitecture.cmake")

__qt_internal_get_static_plugin_condition_genex(QOffscreenIntegrationPlugin condition)
ariadshot_add_module(NAME app KIND STATIC ALLOWED Qt6::Gui)
target_sources(ariadshot_app PRIVATE Fixture.cpp)
target_link_libraries(ariadshot_app PUBLIC Qt6::Gui PRIVATE
    "$<${condition}:Qt6::QOffscreenIntegrationPlugin>"
    "$<${condition}:Qt6::QOffscreenIntegrationPlugin_init>")
