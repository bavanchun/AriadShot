# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

# A core-kind module whose allowed helper links Qt6::Gui. The direct edge is allowed; configuring must fail with the
# reachability rule.
cmake_minimum_required(VERSION 3.28)
project(arch_forbidden_indirect LANGUAGES CXX)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui)
include("${ARIADSHOT_CMAKE_DIR}/AriadShotArchitecture.cmake")

add_library(fixture_helper STATIC Fixture.cpp)
target_link_libraries(fixture_helper PUBLIC Qt6::Gui)

ariadshot_add_module(NAME core KIND STATIC ALLOWED Qt6::Core fixture_helper)
target_sources(ariadshot_core PRIVATE Fixture.cpp)
target_link_libraries(ariadshot_core PUBLIC Qt6::Core PRIVATE fixture_helper)
