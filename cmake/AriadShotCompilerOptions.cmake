# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only
#
# AriadShot::options: warnings, Qt definitions, release hardening and sanitizers for every AriadShot target.

include_guard(GLOBAL)

add_library(ariadshot_options INTERFACE)
add_library(AriadShot::options ALIAS ariadshot_options)

target_compile_features(ariadshot_options INTERFACE cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ariadshot_options INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual -Wcast-qual -Wformat=2
        -Wimplicit-fallthrough)
    if(ARIADSHOT_WERROR)
        target_compile_options(ariadshot_options INTERFACE -Werror)
    endif()
endif()

# QT_NO_CAST_* keep every user-visible string explicit (tr() or QStringLiteral), so all of them stay extractable for
# translation; QT_NO_KEYWORDS avoids clashes with C libraries (use Q_EMIT, Q_SIGNALS, Q_SLOTS).
target_compile_definitions(ariadshot_options INTERFACE
    QT_NO_KEYWORDS
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
    QT_NO_URL_CAST_FROM_STRING
    QT_NO_NARROWING_CONVERSIONS_IN_CONNECT
    QT_DISABLE_DEPRECATED_UP_TO=0x060800)

if(ARIADSHOT_HARDENING)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        include(CheckPIESupported)
        check_pie_supported()
        set(CMAKE_POSITION_INDEPENDENT_CODE ON)
        # -U first: distribution flags may already define _FORTIFY_SOURCE with another level.
        target_compile_options(ariadshot_options INTERFACE
            -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3 -fstack-protector-strong -fstack-clash-protection)
        target_compile_definitions(ariadshot_options INTERFACE _GLIBCXX_ASSERTIONS)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
            target_compile_options(ariadshot_options INTERFACE -fcf-protection)
        endif()
        target_link_options(ariadshot_options INTERFACE LINKER:-z,relro LINKER:-z,now)
    else()
        message(STATUS "ARIADSHOT_HARDENING applies to Linux builds only; macOS hardening is configured with signing.")
    endif()
endif()

if(ARIADSHOT_SANITIZE)
    if(NOT ARIADSHOT_SANITIZE MATCHES "^(address,undefined|address|undefined|thread)$")
        message(FATAL_ERROR
            "ARIADSHOT_SANITIZE must be 'address,undefined', 'address', 'undefined' or 'thread', not "
            "'${ARIADSHOT_SANITIZE}'.")
    endif()
    target_compile_options(ariadshot_options INTERFACE
        -fsanitize=${ARIADSHOT_SANITIZE} -fno-omit-frame-pointer -fno-sanitize-recover=all)
    target_link_options(ariadshot_options INTERFACE -fsanitize=${ARIADSHOT_SANITIZE})
endif()
