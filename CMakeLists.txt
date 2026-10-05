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

# Qt 6.8.3 and older link WrapOpenGL::WrapOpenGL, and so every Qt Gui target, to the AGL framework, which the macOS 26
# SDK no longer ships; Qt later dropped that link upstream ("macOS: Remove linkage to AGL framework"). Qt finds AGL with
# find_library(), which can succeed through the host's /System/Library/Frameworks while the SDK the linker uses has no
# AGL, so whether AGL can be linked is decided by a link test with the real toolchain. When it cannot, the AGL entry
# is removed from the one imported target that carries it.
if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
    include(CheckCXXSourceCompiles)
    include(CMakePushCheckState)
    cmake_push_check_state(RESET)
    set(CMAKE_REQUIRED_LIBRARIES "-framework AGL")
    check_cxx_source_compiles("int main() { return 0; }" ARIADSHOT_LINKER_HAS_AGL)
    cmake_pop_check_state()
    if(NOT ARIADSHOT_LINKER_HAS_AGL)
        get_target_property(_ariadshot_opengl_libs WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
        if(_ariadshot_opengl_libs)
            list(FILTER _ariadshot_opengl_libs EXCLUDE REGEX "^-framework AGL$|(^|/)AGL\\.framework$")
            set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES
                INTERFACE_LINK_LIBRARIES "${_ariadshot_opengl_libs}")
        endif()
    endif()
endif()

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
    add_subdirectory(tools)
endif()

if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    install(FILES packaging/linux/io.github.bavanchun.AriadShot.desktop
            DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
endif()
