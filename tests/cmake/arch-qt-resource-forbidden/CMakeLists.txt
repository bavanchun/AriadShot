# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

# A module that links another target's resource object library. Because resource object libraries are
# only exempt when created by Qt for the target itself, linking another target's resource library must fail.
cmake_minimum_required(VERSION 3.28)
project(arch_qt_resource_forbidden LANGUAGES CXX)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core)
include("${ARIADSHOT_CMAKE_DIR}/AriadShotArchitecture.cmake")

ariadshot_add_module(NAME other KIND STATIC ALLOWED Qt6::Core)
target_sources(ariadshot_other PRIVATE Fixture.cpp)
target_link_libraries(ariadshot_other PUBLIC Qt6::Core)
qt_add_resources(ariadshot_other "other_resources"
    PREFIX "/fixture"
    FILES Fixture.cpp)

ariadshot_add_module(NAME app KIND STATIC ALLOWED Qt6::Core)
target_sources(ariadshot_app PRIVATE Fixture.cpp)
target_link_libraries(ariadshot_app PUBLIC Qt6::Core PRIVATE ariadshot_other_resources_1)
