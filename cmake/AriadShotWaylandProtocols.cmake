# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

include_guard(GLOBAL)

function(ariadshot_generate_wayland_protocols)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "TARGET;OUTPUT_DIR" "XMLS")
    if(NOT arg_TARGET OR NOT arg_XMLS)
        message(FATAL_ERROR "ariadshot_generate_wayland_protocols(TARGET <target> XMLS <xml>...)")
    endif()

    find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

    if(NOT arg_OUTPUT_DIR)
        set(arg_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
    endif()
    file(MAKE_DIRECTORY "${arg_OUTPUT_DIR}")

    set(generated_sources "")
    set(generated_headers "")

    foreach(xml_file IN LISTS arg_XMLS)
        get_filename_component(xml_name "${xml_file}" NAME_WLE)
        set(header "${arg_OUTPUT_DIR}/${xml_name}-client-protocol.h")
        set(source "${arg_OUTPUT_DIR}/${xml_name}-protocol.c")

        add_custom_command(
            OUTPUT "${header}"
            COMMAND "${WAYLAND_SCANNER}" client-header "${xml_file}" "${header}"
            DEPENDS "${xml_file}"
            COMMENT "Generating Wayland client header ${xml_name}-client-protocol.h"
            VERBATIM)

        add_custom_command(
            OUTPUT "${source}"
            COMMAND "${WAYLAND_SCANNER}" private-code "${xml_file}" "${source}"
            DEPENDS "${xml_file}" "${header}"
            COMMENT "Generating Wayland private code ${xml_name}-protocol.c"
            VERBATIM)

        set_source_files_properties("${source}" PROPERTIES
            LANGUAGE CXX
            COMPILE_OPTIONS "-w")

        list(APPEND generated_headers "${header}")
        list(APPEND generated_sources "${source}")
    endforeach()

    target_sources(${arg_TARGET} PRIVATE ${generated_sources} ${generated_headers})
    target_include_directories(${arg_TARGET} PUBLIC "$<BUILD_INTERFACE:${arg_OUTPUT_DIR}>")
endfunction()
