# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

# A module that compiles resources with qt_add_resources (or qt_add_shaders). Qt creates an internal
# resource object library and attaches it via $<TARGET_OBJECTS:...> generator expressions. The internal
# resource objects are part of the target itself and add no link edge, so configuring must succeed.
cmake_minimum_required(VERSION 3.28)
project(arch_qt_resource_allowed LANGUAGES CXX)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core)
include("${ARIADSHOT_CMAKE_DIR}/AriadShotArchitecture.cmake")

ariadshot_add_module(NAME app KIND STATIC ALLOWED Qt6::Core)
target_sources(ariadshot_app PRIVATE Fixture.cpp)
target_link_libraries(ariadshot_app PUBLIC Qt6::Core)
qt_add_resources(ariadshot_app "fixture_resources"
    PREFIX "/fixture"
    FILES Fixture.cpp)
