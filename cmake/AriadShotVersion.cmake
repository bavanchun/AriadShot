# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only
#
# Build-time generation of <build>/generated/core/Version.h from cmake/Version.h.in.
#
# project(VERSION) is the source of truth. The version string is:
#   PROJECT_VERSION                  on the commit tagged v<PROJECT_VERSION>;
#   PROJECT_VERSION+g<short hash>    on any other commit;
#   PROJECT_VERSION                  without a git checkout (source tarballs).
# There is no -dirty suffix, so the string depends only on the commit.
#
# Included as a module, this file provides ariadshot_add_version_header(<target>). Run with cmake -P, it writes the
# header, replacing the file only when its content changes so that unchanged builds stay up to date.

if(CMAKE_SCRIPT_MODE_FILE)
    set(ARIADSHOT_VERSION_STRING "${ARIADSHOT_PROJECT_VERSION}")
    set(ARIADSHOT_GIT_DESCRIPTION "")
    if(EXISTS "${ARIADSHOT_SOURCE_DIR}/.git")
        set(head "")
        set(head_result 1)
        if(ARIADSHOT_GIT_EXECUTABLE)
            execute_process(
                COMMAND "${ARIADSHOT_GIT_EXECUTABLE}" -C "${ARIADSHOT_SOURCE_DIR}" rev-parse --verify HEAD
                OUTPUT_VARIABLE head RESULT_VARIABLE head_result
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        endif()
        if(head_result EQUAL 0)
            execute_process(
                COMMAND "${ARIADSHOT_GIT_EXECUTABLE}" -C "${ARIADSHOT_SOURCE_DIR}"
                        rev-parse -q --verify "refs/tags/v${ARIADSHOT_PROJECT_VERSION}^{commit}"
                OUTPUT_VARIABLE tagged RESULT_VARIABLE tagged_result
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(NOT (tagged_result EQUAL 0 AND tagged STREQUAL head))
                execute_process(
                    COMMAND "${ARIADSHOT_GIT_EXECUTABLE}" -C "${ARIADSHOT_SOURCE_DIR}" rev-parse --short HEAD
                    OUTPUT_VARIABLE short_head
                    OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
                set(ARIADSHOT_GIT_DESCRIPTION "g${short_head}")
                set(ARIADSHOT_VERSION_STRING "${ARIADSHOT_PROJECT_VERSION}+${ARIADSHOT_GIT_DESCRIPTION}")
            endif()
        else()
            message(WARNING
                "git could not read the commit of ${ARIADSHOT_SOURCE_DIR}; the version string is "
                "${ARIADSHOT_VERSION_STRING} without a commit hash.")
        endif()
    endif()
    configure_file("${ARIADSHOT_TEMPLATE}" "${ARIADSHOT_OUTPUT}.tmp" @ONLY)
    file(COPY_FILE "${ARIADSHOT_OUTPUT}.tmp" "${ARIADSHOT_OUTPUT}" ONLY_IF_DIFFERENT)
    file(REMOVE "${ARIADSHOT_OUTPUT}.tmp")
    return()
endif()

include_guard(GLOBAL)

find_package(Git QUIET)

# Regenerates Version.h on every build and makes <target> depend on it and include it privately.
function(ariadshot_add_version_header target)
    set(output_dir "${PROJECT_BINARY_DIR}/generated")
    set(output "${output_dir}/core/Version.h")
    add_custom_target(ariadshot_version
        COMMAND "${CMAKE_COMMAND}"
                "-DARIADSHOT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DARIADSHOT_PROJECT_VERSION=${PROJECT_VERSION}"
                "-DARIADSHOT_GIT_EXECUTABLE=${GIT_EXECUTABLE}"
                "-DARIADSHOT_BUILD_TYPE=$<CONFIG>"
                "-DARIADSHOT_TEMPLATE=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/Version.h.in"
                "-DARIADSHOT_OUTPUT=${output}"
                -P "${CMAKE_CURRENT_FUNCTION_LIST_FILE}"
        BYPRODUCTS "${output}"
        COMMENT "Generating core/Version.h"
        VERBATIM)
    add_dependencies(${target} ariadshot_version)
    target_include_directories(${target} PRIVATE "${output_dir}")
endfunction()
