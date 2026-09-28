# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

# A core-kind module that receives a Qt Gui plugin through Qt's plugin-import expression. The edge counts as Qt6::Gui,
# so configuring must fail with the direct-edge rule.
cmake_minimum_required(VERSION 3.28)
project(arch_qt_plugin_forbidden LANGUAGES CXX)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui)
include("${ARIADSHOT_CMAKE_DIR}/AriadShotArchitecture.cmake")

__qt_internal_get_static_plugin_condition_genex(QOffscreenIntegrationPlugin condition)
ariadshot_add_module(NAME core KIND STATIC ALLOWED Qt6::Core)
target_sources(ariadshot_core PRIVATE Fixture.cpp)
target_link_libraries(ariadshot_core PUBLIC Qt6::Core PRIVATE "$<${condition}:Qt6::QOffscreenIntegrationPlugin>")
