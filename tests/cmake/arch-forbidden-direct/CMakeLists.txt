# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

# A core-kind module that links Qt6::Gui directly. Configuring must fail with the direct-edge rule.
cmake_minimum_required(VERSION 3.28)
project(arch_forbidden_direct LANGUAGES CXX)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui)
include("${ARIADSHOT_CMAKE_DIR}/AriadShotArchitecture.cmake")

ariadshot_add_module(NAME core KIND STATIC ALLOWED Qt6::Core)
target_sources(ariadshot_core PRIVATE Fixture.cpp)
target_link_libraries(ariadshot_core PUBLIC Qt6::Core Qt6::Gui)
