# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only
#
# Every test runs in a neutral environment (docs/dev/testing.md): the offscreen platform, none of the host's Qt
# variables, no session bus, and a fresh home and XDG directory set per test under this build directory. CTest names
# are <area>.<topic>; labels are unit, golden, wayland, nested-hyprland, live and tsan-safe.

set(ARIADSHOT_TEST_ROOT "${CMAKE_CURRENT_BINARY_DIR}/environment")

# Applies the neutral environment to an existing test and makes it require the directory set-up fixture.
function(ariadshot_test_environment name)
    set(root "${ARIADSHOT_TEST_ROOT}/${name}")
    set_property(GLOBAL APPEND PROPERTY ARIADSHOT_TEST_NAMES "${name}")
    set_tests_properties("${name}" PROPERTIES
        FIXTURES_REQUIRED ariadshot_test_environment
        ENVIRONMENT_MODIFICATION
            "QT_QPA_PLATFORM=set:offscreen;\
QT_QPA_PLATFORMTHEME=unset:;\
QT_STYLE_OVERRIDE=unset:;\
QT_IM_MODULE=unset:;\
QT_PLUGIN_PATH=unset:;\
QT_SCALE_FACTOR=unset:;\
QT_SCREEN_SCALE_FACTORS=unset:;\
QT_AUTO_SCREEN_SCALE_FACTOR=unset:;\
QT_ENABLE_HIGHDPI_SCALING=unset:;\
QT_FONT_DPI=unset:;\
DBUS_SESSION_BUS_ADDRESS=unset:;\
HOME=set:${root}/home;\
XDG_CONFIG_HOME=set:${root}/config;\
XDG_DATA_HOME=set:${root}/data;\
XDG_CACHE_HOME=set:${root}/cache;\
XDG_STATE_HOME=set:${root}/state;\
XDG_RUNTIME_DIR=set:${root}/runtime;\
ARIADSHOT_TEST_OUTPUT_DIR=set:${root}/output")
    if(ARIADSHOT_SANITIZE STREQUAL "thread")
        set_property(TEST "${name}" APPEND PROPERTY ENVIRONMENT_MODIFICATION
            "TSAN_OPTIONS=set:suppressions=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/sanitizers/tsan.supp")
    endif()
endfunction()

# ariadshot_add_test(NAME <area>.<topic> SOURCES <files>... [LIBS <targets>...] [LABELS <labels>...])
function(ariadshot_add_test)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "NAME" "SOURCES;LIBS;LABELS")
    if(arg_UNPARSED_ARGUMENTS OR NOT arg_NAME OR NOT arg_SOURCES)
        message(FATAL_ERROR "ariadshot_add_test(NAME <name> SOURCES <files>... [LIBS ...] [LABELS ...])")
    endif()
    string(REGEX REPLACE "[.-]" "_" target "tst_${arg_NAME}")
    add_executable(${target} ${arg_SOURCES})
    target_link_libraries(${target} PRIVATE Qt6::Test AriadShot::options ${arg_LIBS})
    add_test(NAME ${arg_NAME} COMMAND ${target})
    set_tests_properties(${arg_NAME} PROPERTIES LABELS "${arg_LABELS}")
    ariadshot_test_environment(${arg_NAME})
endfunction()

add_library(ariadshot_test_support STATIC support/ImageCompare.h support/ImageCompare.cpp)
target_include_directories(ariadshot_test_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ariadshot_test_support PUBLIC Qt6::Gui PRIVATE AriadShot::options)

ariadshot_add_test(NAME support.image-compare
    SOURCES support/tst_ImageCompare.cpp
    LIBS ariadshot_test_support
    LABELS unit)

ariadshot_add_test(NAME core.build-info
    SOURCES unit/core/tst_BuildInfo.cpp
    LIBS AriadShot::core
    LABELS unit tsan-safe)
target_compile_definitions(tst_core_build_info PRIVATE ARIADSHOT_TEST_PROJECT_VERSION="${PROJECT_VERSION}")

ariadshot_add_test(NAME render.canonical-image
    SOURCES unit/render/tst_CanonicalImage.cpp
    LIBS AriadShot::render
    LABELS unit)

ariadshot_add_test(NAME render.smoke-golden
    SOURCES golden/tst_RenderSmoke.cpp
    LIBS AriadShot::render ariadshot_test_support
    LABELS golden)
target_compile_definitions(tst_render_smoke_golden PRIVATE ARIADSHOT_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")

ariadshot_add_test(NAME app.tray-controller
    SOURCES unit/app/tst_TrayController.cpp
    LIBS AriadShot::app
    LABELS unit)

ariadshot_add_test(NAME app.signal-bridge
    SOURCES unit/app/tst_SignalBridge.cpp
    LIBS AriadShot::app
    LABELS unit)

# --- Tests of the build system and the repository scripts ------------------------------------------------------------
#
# Negative cases pass only when the checker prints its own message for the rule under test (PASS_REGULAR_EXPRESSION),
# so an unrelated failure cannot make them green.

find_program(ARIADSHOT_BASH bash REQUIRED)
find_program(ARIADSHOT_JQ jq REQUIRED)

# Reads a tab-separated case file into the list variable <out>, one element per case line, with tabs turned into
# "|TAB|" and semicolons into "|SEMICOLON|" so that CMake's list handling cannot split a case.
function(ariadshot_read_cases path out)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
    file(READ "${path}" content)
    string(REPLACE ";" "|SEMICOLON|" content "${content}")
    string(REPLACE "\t" "|TAB|" content "${content}")
    string(REPLACE "\n" ";" lines "${content}")
    set(cases "")
    foreach(line IN LISTS lines)
        if(NOT line STREQUAL "" AND NOT line MATCHES "^#")
            list(APPEND cases "${line}")
        endif()
    endforeach()
    set(${out} "${cases}" PARENT_SCOPE)
endfunction()

# Splits one case line from ariadshot_read_cases() into <count> fields named <prefix>0 ... ; the last field keeps the
# rest of the line.
function(ariadshot_split_case line count prefix)
    set(rest "${line}")
    math(EXPR last "${count} - 1")
    foreach(index RANGE ${last})
        if(index EQUAL last)
            set(field "${rest}")
        else()
            string(FIND "${rest}" "|TAB|" position)
            string(SUBSTRING "${rest}" 0 ${position} field)
            math(EXPR position "${position} + 5")
            string(SUBSTRING "${rest}" ${position} -1 rest)
        endif()
        string(REPLACE "|SEMICOLON|" ";" field "${field}")
        string(REPLACE "|TAB|" "\t" field "${field}")
        set(${prefix}${index} "${field}" PARENT_SCOPE)
    endforeach()
endfunction()

# Architecture check fixtures: standalone projects configured with the real cmake/AriadShotArchitecture.cmake.
function(ariadshot_add_architecture_fixture fixture)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "EXPECT" "")
    set(name "build.${fixture}")
    add_test(NAME ${name}
        COMMAND "${CMAKE_COMMAND}" --fresh
                -S "${CMAKE_CURRENT_SOURCE_DIR}/cmake/${fixture}"
                -B "${CMAKE_CURRENT_BINARY_DIR}/cmake-fixtures/${fixture}"
                -G "${CMAKE_GENERATOR}"
                "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
                "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
                "-DQt6_DIR=${Qt6_DIR}"
                "-DARIADSHOT_CMAKE_DIR=${PROJECT_SOURCE_DIR}/cmake")
    if(arg_EXPECT)
        set_tests_properties(${name} PROPERTIES PASS_REGULAR_EXPRESSION "${arg_EXPECT}")
    else()
        set_tests_properties(${name} PROPERTIES FAIL_REGULAR_EXPRESSION "ariadshot-arch:")
    endif()
    set_tests_properties(${name} PROPERTIES LABELS unit)
    ariadshot_test_environment(${name})
endfunction()

ariadshot_add_architecture_fixture(arch-forbidden-direct
    EXPECT "ariadshot-arch: core may not link Qt6::Gui \\(allowed: Qt6::Core\\)")
ariadshot_add_architecture_fixture(arch-forbidden-indirect
    EXPECT "ariadshot-arch: core may not reach Qt6::Gui \\(core -> fixture_helper -> Qt6::Gui\\)")
ariadshot_add_architecture_fixture(arch-allowed-transitive)

# Include rules of scripts/check-architecture.sh, on small source trees.
function(ariadshot_add_include_case case)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "EXPECT" "")
    set(name "scripts.arch-include.${case}")
    add_test(NAME ${name}
        COMMAND "${ARIADSHOT_BASH}" "${PROJECT_SOURCE_DIR}/scripts/check-architecture.sh"
                --root "${CMAKE_CURRENT_SOURCE_DIR}/scripts/arch-include-cases/${case}")
    if(arg_EXPECT)
        set_tests_properties(${name} PROPERTIES PASS_REGULAR_EXPRESSION "${arg_EXPECT}")
    else()
        set_tests_properties(${name} PROPERTIES FAIL_REGULAR_EXPRESSION "ariadshot-arch:")
    endif()
    set_tests_properties(${name} PROPERTIES LABELS unit)
    ariadshot_test_environment(${name})
    set_property(TEST ${name} APPEND PROPERTY ENVIRONMENT_MODIFICATION
        "ARIADSHOT_QT_HEADERS=set:${QT6_INSTALL_PREFIX}/${QT6_INSTALL_HEADERS}"
        "ARIADSHOT_QT_LIBS=set:${QT6_INSTALL_PREFIX}/${QT6_INSTALL_LIBS}")
endfunction()

ariadshot_add_include_case(allowed)
ariadshot_add_include_case(core-includes-qtgui
    EXPECT "ariadshot-arch: src/core/Pixels.cpp:4: core may not include <QImage> \\(QtGui\\)")
ariadshot_add_include_case(core-includes-render
    EXPECT "ariadshot-arch: src/core/Uses.cpp:4: core may not include \"render/Canvas.h\"")
ariadshot_add_include_case(header-pulls-in-widgets
    EXPECT "ariadshot-arch: src/ui/View.cpp:4: ui may not include <QWidget> \\(QtWidgets\\) through render/Painter.h")
ariadshot_add_include_case(platform-header-in-render
    EXPECT "ariadshot-arch: src/render/Surface.cpp:4: render may not include the platform header <wayland-client.h>")
ariadshot_add_include_case(private-api-in-ui
    EXPECT "ariadshot-arch: src/ui/Window.cpp:4: ui may not include <QtGui/private/qguiapplication_p.h> \\(Qt private API\\)")
ariadshot_add_include_case(desktop-name-outside-backends
    EXPECT "ariadshot-arch: src/ui/Snap.cpp:7: desktop-name check outside src/backends/")

# Commit message rules: one test per line of scripts/commit-message-cases.txt.
ariadshot_read_cases("${CMAKE_CURRENT_SOURCE_DIR}/scripts/commit-message-cases.txt" commit_cases)
set(case_number 0)
foreach(line IN LISTS commit_cases)
    math(EXPR case_number "${case_number} + 1")
    ariadshot_split_case("${line}" 3 field)
    string(REPLACE "\\n" "\n" message "${field2}")
    set(message_file "${CMAKE_CURRENT_BINARY_DIR}/commit-message-cases/case-${case_number}.txt")
    file(WRITE "${message_file}" "${message}\n")
    set(mode_arguments "")
    if(field0 STREQUAL "pr-title")
        set(mode_arguments --pr-title)
    elseif(field0 STREQUAL "pr-body")
        set(mode_arguments --pr-body)
    elseif(NOT field0 STREQUAL "commit")
        message(FATAL_ERROR "commit-message-cases.txt case ${case_number}: unknown mode '${field0}'")
    endif()
    set(name "scripts.commit-message.${case_number}-${field0}-${field1}")
    add_test(NAME ${name}
        COMMAND "${ARIADSHOT_BASH}" "${PROJECT_SOURCE_DIR}/scripts/check-commit-message.sh" ${mode_arguments}
                "${message_file}")
    if(field1 STREQUAL "accept")
        set_tests_properties(${name} PROPERTIES FAIL_REGULAR_EXPRESSION "rejected")
    else()
        set_tests_properties(${name} PROPERTIES PASS_REGULAR_EXPRESSION "check-commit-message: rejected \\[${field1}\\]")
    endif()
    set_tests_properties(${name} PROPERTIES LABELS unit)
    ariadshot_test_environment(${name})
endforeach()

# The agent guard: every case of scripts/agent-guard-cases.tsv through each runtime's adapter.
set(guard_cases_file "${CMAKE_CURRENT_SOURCE_DIR}/scripts/agent-guard-cases.tsv")
ariadshot_read_cases("${guard_cases_file}" guard_cases)
foreach(line IN LISTS guard_cases)
    ariadshot_split_case("${line}" 6 field)
    string(REPLACE "/" ";" runtime_tools "${field1}")
    list(GET runtime_tools 0 claude_tool)
    list(GET runtime_tools 1 agy_tool)
    set(runtimes claude agy)
    set(runtime_tools ${claude_tool} ${agy_tool})
    foreach(runtime tool IN ZIP_LISTS runtimes runtime_tools)
        if(tool STREQUAL "-")
            continue()
        endif()
        set(name "scripts.agent-guard.${runtime}.${field0}")
        add_test(NAME ${name}
            COMMAND "${ARIADSHOT_BASH}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-agent-guard-case.sh"
                    "${guard_cases_file}" "${field0}" ${runtime})
        if(field4 STREQUAL "allow")
            set(verdict "allow")
        else()
            string(REPLACE "deny:" "" rule "${field4}")
            set(verdict "deny \\[${rule}\\]")
        endif()
        set_tests_properties(${name} PROPERTIES
            PASS_REGULAR_EXPRESSION "guard-case: ${field0} ${runtime} -> ${verdict}"
            LABELS unit)
        ariadshot_test_environment(${name})
    endforeach()
endforeach()

# The owner gate's inspect decision, on recorded pull request payloads.
function(ariadshot_add_gate_case case)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "EXPECT" "")
    set(directory "${CMAKE_CURRENT_SOURCE_DIR}/scripts/gate-inspect-cases/${case}")
    set(name "scripts.gate-inspect.${case}")
    add_test(NAME ${name}
        COMMAND "${ARIADSHOT_BASH}" "${PROJECT_SOURCE_DIR}/scripts/github/gate-inspect.sh"
                --event "${directory}/event.json" --files "${directory}/files.json")
    set_tests_properties(${name} PROPERTIES PASS_REGULAR_EXPRESSION "${arg_EXPECT}" LABELS unit)
    ariadshot_test_environment(${name})
endfunction()

ariadshot_add_gate_case(fork-touching-workflows
    EXPECT "gate-inspect: fail: fork pull request changes workflows")
ariadshot_add_gate_case(fork-not-touching-workflows EXPECT "gate-inspect: pass")
ariadshot_add_gate_case(same-repository-touching-workflows EXPECT "gate-inspect: pass")
ariadshot_add_gate_case(fork-renaming-a-workflow-away
    EXPECT "gate-inspect: fail: fork pull request changes workflows")
ariadshot_add_gate_case(deleted-fork-touching-workflows
    EXPECT "gate-inspect: fail: fork pull request changes workflows")
ariadshot_add_gate_case(incomplete-file-list EXPECT "gate-inspect: fail: the changed-file list has 2 entries")

# Repository hygiene: what check-repo.sh sees in the index and in a commit, in throwaway repositories.
function(ariadshot_add_repo_case case)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "EXPECT" "")
    set(name "scripts.check-repo.${case}")
    add_test(NAME ${name}
        COMMAND "${ARIADSHOT_BASH}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-check-repo-case.sh" ${case})
    set_tests_properties(${name} PROPERTIES PASS_REGULAR_EXPRESSION "${arg_EXPECT}" LABELS unit)
    ariadshot_test_environment(${name})
endfunction()

ariadshot_add_repo_case(staged-path-hidden-by-working-copy EXPECT "check-repo: notes.txt:1:.*: private path")
ariadshot_add_repo_case(reference-and-plans-paths EXPECT "check-repo: notes.txt:1:.* and .*: private path")
ariadshot_add_repo_case(clean-tree EXPECT "check-repo: no private paths or per-runtime instruction files")
ariadshot_add_repo_case(untracked-instruction-file
    EXPECT "check-repo: CLAUDE.md: per-runtime instruction files are not allowed")
ariadshot_add_repo_case(private-directory-staged EXPECT "check-repo: [A-Za-z]+/reference-note.txt: private file path")
ariadshot_add_repo_case(private-directory-committed EXPECT "check-repo: docs/[a-z]+/plan-note.txt: private file path")

# --- The fresh per-test directories -------------------------------------------------------------------------------

get_property(test_names GLOBAL PROPERTY ARIADSHOT_TEST_NAMES)
set(reset_script "${CMAKE_CURRENT_BINARY_DIR}/reset-test-environment.cmake")
file(CONFIGURE OUTPUT "${reset_script}" @ONLY CONTENT [=[
file(REMOVE_RECURSE "@ARIADSHOT_TEST_ROOT@")
foreach(name IN ITEMS @test_names@)
    foreach(directory IN ITEMS home config data cache state output)
        file(MAKE_DIRECTORY "@ARIADSHOT_TEST_ROOT@/${name}/${directory}")
    endforeach()
    file(MAKE_DIRECTORY "@ARIADSHOT_TEST_ROOT@/${name}/runtime")
    file(CHMOD "@ARIADSHOT_TEST_ROOT@/${name}/runtime" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
endforeach()
]=])
add_test(NAME test-environment.reset COMMAND "${CMAKE_COMMAND}" -P "${reset_script}")
set_tests_properties(test-environment.reset PROPERTIES FIXTURES_SETUP ariadshot_test_environment)
