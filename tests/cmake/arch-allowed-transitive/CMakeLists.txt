# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

# render-kind -> core-kind -> Qt6::Core, plus render -> Qt6::Gui: every edge is allowed, so configuring must succeed.
cmake_minimum_required(VERSION 3.28)
project(arch_allowed_transitive LANGUAGES CXX)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui)
include("${ARIADSHOT_CMAKE_DIR}/AriadShotArchitecture.cmake")

ariadshot_add_module(NAME core KIND STATIC ALLOWED Qt6::Core)
target_sources(ariadshot_core PRIVATE Fixture.cpp)
target_link_libraries(ariadshot_core PUBLIC Qt6::Core)

ariadshot_add_module(NAME render KIND STATIC ALLOWED AriadShot::core Qt6::Gui)
target_sources(ariadshot_render PRIVATE Fixture.cpp)
target_link_libraries(ariadshot_render PUBLIC AriadShot::core Qt6::Gui)
