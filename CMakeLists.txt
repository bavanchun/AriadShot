# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.28)

# macOS minimum; ignored on other platforms. Must be set before project().
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version")

project(AriadShot VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

option(BUILD_TESTING "Build the test suite" ON)
option(ARIADSHOT_WERROR "Treat compiler warnings as errors" OFF)
option(ARIADSHOT_HARDENING "Enable release hardening flags (Linux)" OFF)
set(ARIADSHOT_SANITIZE "" CACHE STRING "Sanitizers to enable: empty, 'address,undefined' or 'thread'")
set(ARIADSHOT_EXPECT_QT_VERSION "" CACHE STRING "Fail the configure step unless the Qt version found equals this")

# The architecture check (cmake/AriadShotArchitecture.cmake) runs at the end of this directory and must see imported
# targets that modules find in their own directories.
set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)
if(BUILD_TESTING)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
endif()
qt_standard_project_setup(REQUIRES 6.8)

if(ARIADSHOT_EXPECT_QT_VERSION AND NOT Qt6_VERSION VERSION_EQUAL ARIADSHOT_EXPECT_QT_VERSION)
    message(FATAL_ERROR
        "Qt ${Qt6_VERSION} was found, but this build expects Qt ${ARIADSHOT_EXPECT_QT_VERSION} "
        "(ARIADSHOT_EXPECT_QT_VERSION).")
endif()

set(_ariadshot_toolchain_summary
    "Qt ${Qt6_VERSION}, ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}, CMake ${CMAKE_VERSION}")
message(STATUS "AriadShot toolchain: ${_ariadshot_toolchain_summary}")
if(DEFINED ENV{GITHUB_STEP_SUMMARY} AND NOT "$ENV{GITHUB_STEP_SUMMARY}" STREQUAL "")
    file(APPEND "$ENV{GITHUB_STEP_SUMMARY}" "AriadShot toolchain: ${_ariadshot_toolchain_summary}\n\n")
endif()

include(AriadShotCompilerOptions)
include(AriadShotArchitecture)
include(AriadShotVersion)

add_subdirectory(src)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    install(FILES packaging/linux/io.github.bavanchun.AriadShot.desktop
            DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
endif()
