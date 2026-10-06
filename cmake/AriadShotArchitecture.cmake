# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only
#
# Module targets and the configure-time architecture check (docs/spec/02-modules-and-interfaces.md §1 and §2).
#
#   ariadshot_add_module(NAME <module> KIND STATIC|INTERFACE ALLOWED <target>...)
#       Creates ariadshot_<module> (a "/" in <module> becomes "_") with the alias AriadShot::<module>. ALLOWED is the
#       module's "may link" column: every direct link entry must be one of these targets or AriadShot::options. An
#       INTERFACE module declares its place in the graph and carries no sources; it becomes STATIC in the change that
#       adds its first real class.
#
#   ariadshot_architecture_register(TARGET <target> NAME <name> ALLOWED <target>...)
#       Applies the same direct-edge rule to a target created elsewhere (the daemon executable).
#
# Including this file defers ariadshot_check_architecture() to the end of the top-level directory, after every target
# exists. The check applies three rules and fails the configure step with lines that start with "ariadshot-arch:".
#
#   1. Direct edges: each entry of LINK_LIBRARIES and INTERFACE_LINK_LIBRARIES must be in ALLOWED.
#   2. Reachability for the portable modules core, render and ui: the transitive closure through project targets must
#      not contain a forbidden target (see _ariadshot_arch_forbidden). Imported targets are leaves compared by name,
#      so allowed paths such as render -> core -> Qt6::Core pass.
#   3. Normalisation: aliases resolve to their real target; $<LINK_ONLY:x> and $<BUILD_INTERFACE:x> are unwrapped;
#      Qt's plugin-import expression counts as an edge to the Qt module that the plugin extends (see
#      _ariadshot_arch_qt_plugin_module); an internal resource object library that Qt generates for a target (through
#      qt_add_resources or qt_add_shaders) and its $<TARGET_OBJECTS:...> propagation expressions are treated as part
#      of that target and add no link edge; any other generator expression, plain library path or linker flag fails
#      closed until a rule is added here.

include_guard(GLOBAL)

# Libraries that belong to platform backends and surface hosts. The portable modules may never reach them.
set(ARIADSHOT_ARCH_PLATFORM_PATTERN
    "^(LayerShellQt|PkgConfig|Wayland|X11|XCB|Xcb|PipeWire|Libdrm|Gbm)::|^Qt6?::(DBus|Wayland[A-Za-z]*|Xcb[A-Za-z]*)$")

function(ariadshot_architecture_register)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "TARGET;NAME" "ALLOWED")
    if(arg_UNPARSED_ARGUMENTS OR NOT arg_TARGET OR NOT arg_NAME)
        message(FATAL_ERROR "ariadshot_architecture_register(TARGET <target> NAME <name> ALLOWED <target>...)")
    endif()
    set_target_properties(${arg_TARGET} PROPERTIES
        ARIADSHOT_ARCH_NAME "${arg_NAME}"
        ARIADSHOT_ARCH_ALLOWED "${arg_ALLOWED}")
    set_property(GLOBAL APPEND PROPERTY ARIADSHOT_ARCH_TARGETS ${arg_TARGET})
endfunction()

function(ariadshot_add_module)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "NAME;KIND" "ALLOWED")
    if(arg_UNPARSED_ARGUMENTS OR NOT arg_NAME OR NOT arg_KIND MATCHES "^(STATIC|INTERFACE)$")
        message(FATAL_ERROR "ariadshot_add_module(NAME <module> KIND STATIC|INTERFACE ALLOWED <target>...)")
    endif()
    string(REPLACE "/" "_" id "${arg_NAME}")
    add_library(ariadshot_${id} ${arg_KIND})
    add_library(AriadShot::${id} ALIAS ariadshot_${id})
    if(arg_KIND STREQUAL "STATIC")
        target_include_directories(ariadshot_${id} PUBLIC "${PROJECT_SOURCE_DIR}/src")
        if(TARGET AriadShot::options)
            target_link_libraries(ariadshot_${id} PRIVATE AriadShot::options)
        endif()
    endif()
    ariadshot_architecture_register(TARGET ariadshot_${id} NAME "${arg_NAME}" ALLOWED ${arg_ALLOWED})
endfunction()

# Resolves an alias to the target it names.
function(_ariadshot_arch_canonical name out)
    if(TARGET "${name}")
        get_target_property(aliased "${name}" ALIASED_TARGET)
        if(aliased)
            set(name "${aliased}")
        endif()
    endif()
    set(${out} "${name}" PARENT_SCOPE)
endfunction()

# Sets <out> to the Qt module target that the Qt plugin <plugin> extends, or to "" when <plugin> is not a Qt plugin.
# Qt's target finalizer links every static plugin of a Qt module into the executables that use the module, as
# $<condition:Qt6::<Plugin>> and $<condition:Qt6::<Plugin>_init>, the object library that registers it; on Apple the
# permission plugins are static even in a shared Qt. Such an edge is attributed to the plugin's module (the QT_MODULE
# property of Qt's imported plugin target), so the rules of this file apply to that module.
function(_ariadshot_arch_qt_plugin_module plugin out)
    set(module "")
    set(candidates "${plugin}")
    if(plugin MATCHES "^(.+)_init$")
        list(APPEND candidates "${CMAKE_MATCH_1}")
    endif()
    foreach(candidate IN LISTS candidates)
        if(NOT TARGET "${candidate}")
            continue()
        endif()
        get_target_property(plugin_type "${candidate}" QT_PLUGIN_TYPE)
        get_target_property(qt_module "${candidate}" QT_MODULE)
        if(NOT plugin_type OR NOT qt_module OR NOT candidate MATCHES "^([A-Za-z0-9_]+)::")
            continue()
        endif()
        # The module target lives in the plugin's namespace (Qt6::).
        if(TARGET "${CMAKE_MATCH_1}::${qt_module}")
            _ariadshot_arch_canonical("${CMAKE_MATCH_1}::${qt_module}" module)
            break()
        endif()
    endforeach()
    set(${out} "${module}" PARENT_SCOPE)
endfunction()

# Sets <out> to TRUE when <candidate> is an internal resource object library created by Qt for <target>.
# Qt's qt_add_resources and qt_add_shaders create static resource object libraries named <target>_resources_<n>
# and mark them with _is_qt_propagated_object_library and _qt_resource_name.
function(_ariadshot_arch_is_target_resource_library candidate target out)
    set(is_resource FALSE)
    if(candidate MATCHES "^${target}_resources_[0-9]+$" AND TARGET "${candidate}")
        get_target_property(type "${candidate}" TYPE)
        get_target_property(is_qt_res "${candidate}" _is_qt_propagated_object_library)
        get_target_property(res_name "${candidate}" _qt_resource_name)
        if(type STREQUAL "OBJECT_LIBRARY" AND (is_qt_res OR res_name))
            set(is_resource TRUE)
        endif()
    endif()
    set(${out} ${is_resource} PARENT_SCOPE)
endfunction()

# Returns the normalised direct link entries of <target>, and error lines for entries the check cannot interpret.
function(_ariadshot_arch_direct_entries target label out_entries out_errors)
    set(entries "")
    set(errors "")
    foreach(property IN ITEMS LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(values ${target} ${property})
        if(NOT values)
            continue()
        endif()
        foreach(value IN LISTS values)
            # Directory markers that target_link_libraries() inserts when called from another directory.
            if(value MATCHES "^::@")
                continue()
            endif()
            while(value MATCHES "^\\$<(LINK_ONLY|BUILD_INTERFACE):(.*)>$")
                set(value "${CMAKE_MATCH_2}")
            endwhile()
            # Qt's plugin-import expression: a condition that carries Qt's own marker, selecting one Qt target.
            if(value MATCHES "^\\$<\\$<.*\\$<BOOL:QT_IS_PLUGIN_GENEX>.*>:([A-Za-z0-9_]+::[A-Za-z0-9_]+)>$")
                set(plugin "${CMAKE_MATCH_1}")
                _ariadshot_arch_qt_plugin_module("${plugin}" value)
                if(NOT value)
                    list(APPEND errors "ariadshot-arch: ${label} imports ${plugin}, which is not a Qt module's plugin")
                    continue()
                endif()
            endif()
            # Qt's resource object propagation expression for this target's own compiled resources.
            if(value MATCHES "^(\\$<.*:)?\\$<TARGET_OBJECTS:(\\$<TARGET_NAME:)?([A-Za-z0-9_]+)>+$")
                set(res_target "${CMAKE_MATCH_3}")
                _ariadshot_arch_is_target_resource_library("${res_target}" "${target}" is_own_resource)
                if(is_own_resource)
                    continue()
                endif()
            endif()
            _ariadshot_arch_is_target_resource_library("${value}" "${target}" is_own_target_res)
            if(is_own_target_res)
                continue()
            endif()
            if(value MATCHES "\\$<")
                list(APPEND errors "ariadshot-arch: ${label} uses an unsupported generator expression: ${value}")
            elseif(NOT TARGET "${value}")
                list(APPEND errors "ariadshot-arch: ${label} links a plain library path or flag: ${value}")
            else()
                _ariadshot_arch_canonical("${value}" value)
                list(APPEND entries "${value}")
            endif()
        endforeach()
    endforeach()
    list(REMOVE_DUPLICATES entries)
    set(${out_entries} "${entries}" PARENT_SCOPE)
    set(${out_errors} "${errors}" PARENT_SCOPE)
endfunction()

# Sets <out> to TRUE when the portable module <module> may not reach <dependency>.
function(_ariadshot_arch_forbidden module dependency out)
    set(forbidden FALSE)
    if(dependency MATCHES "${ARIADSHOT_ARCH_PLATFORM_PATTERN}")
        set(forbidden TRUE)
    elseif(module STREQUAL "core")
        # core is QtCore-only.
        if(dependency MATCHES "^Qt6?::" AND NOT dependency MATCHES "^Qt6?::Core$")
            set(forbidden TRUE)
        endif()
    elseif(module MATCHES "^(render|ui)$")
        # render and ui are QtGui-only: no widgets, no Qt Quick, no private GUI API.
        if(dependency MATCHES "^Qt6?::([A-Za-z]*Widgets|Quick[A-Za-z]*|Qml[A-Za-z]*|[A-Za-z]+Private)$")
            set(forbidden TRUE)
        endif()
    endif()
    set(${out} ${forbidden} PARENT_SCOPE)
endfunction()

function(_ariadshot_arch_check_target target violations_var)
    set(violations "${${violations_var}}")
    get_target_property(name ${target} ARIADSHOT_ARCH_NAME)
    get_target_property(allowed ${target} ARIADSHOT_ARCH_ALLOWED)
    if(NOT allowed)
        set(allowed "")
    endif()

    set(allowed_canonical "")
    foreach(entry IN LISTS allowed)
        _ariadshot_arch_canonical("${entry}" entry)
        list(APPEND allowed_canonical "${entry}")
    endforeach()
    set(options_target "")
    if(TARGET AriadShot::options)
        _ariadshot_arch_canonical(AriadShot::options options_target)
    endif()

    # Rule 1: direct edges.
    _ariadshot_arch_direct_entries(${target} "${name}" entries errors)
    list(APPEND violations ${errors})
    foreach(entry IN LISTS entries)
        if(NOT entry STREQUAL options_target AND NOT entry IN_LIST allowed_canonical)
            string(REPLACE ";" ", " allowed_text "${allowed}")
            list(APPEND violations "ariadshot-arch: ${name} may not link ${entry} (allowed: ${allowed_text})")
        endif()
    endforeach()

    # Rule 2: reachability for the portable modules.
    if(name MATCHES "^(core|render|ui)$")
        set(queue ${target})
        set(visited "")
        set(chain_${target} "${name}")
        while(queue)
            list(POP_FRONT queue current)
            if(current IN_LIST visited)
                continue()
            endif()
            list(APPEND visited ${current})
            if(NOT current STREQUAL target)
                _ariadshot_arch_direct_entries(${current} "${name} (through ${chain_${current}})" entries errors)
                list(APPEND violations ${errors})
            endif()
            foreach(entry IN LISTS entries)
                if(NOT DEFINED chain_${entry})
                    set(chain_${entry} "${chain_${current}} -> ${entry}")
                endif()
                _ariadshot_arch_forbidden("${name}" "${entry}" forbidden)
                if(forbidden)
                    list(APPEND violations "ariadshot-arch: ${name} may not reach ${entry} (${chain_${entry}})")
                    continue()
                endif()
                get_target_property(imported ${entry} IMPORTED)
                if(NOT imported)
                    list(APPEND queue ${entry})
                endif()
            endforeach()
        endwhile()
    endif()

    set(${violations_var} "${violations}" PARENT_SCOPE)
endfunction()

function(ariadshot_check_architecture)
    get_property(targets GLOBAL PROPERTY ARIADSHOT_ARCH_TARGETS)
    set(violations "")
    foreach(target IN LISTS targets)
        _ariadshot_arch_check_target(${target} violations)
    endforeach()
    list(REMOVE_DUPLICATES violations)
    if(violations)
        foreach(line IN LISTS violations)
            message(NOTICE "${line}")
        endforeach()
        list(LENGTH violations count)
        message(FATAL_ERROR "ariadshot-arch: ${count} architecture violation(s); see the lines above.")
    endif()
endfunction()

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL ariadshot_check_architecture)
