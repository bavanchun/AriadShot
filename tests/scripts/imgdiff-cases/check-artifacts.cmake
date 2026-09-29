# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

if(NOT DEFINED IMGDIFF OR NOT DEFINED ACTUAL OR NOT DEFINED EXPECTED OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "imgdiff artifact case requires IMGDIFF, ACTUAL, EXPECTED and OUTPUT_DIR")
endif()

file(REMOVE_RECURSE "${OUTPUT_DIR}")
execute_process(
    COMMAND "${IMGDIFF}" --class exact --out "${OUTPUT_DIR}" "${ACTUAL}" "${EXPECTED}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
if(NOT result EQUAL 1)
    message(FATAL_ERROR "imgdiff returned ${result}, expected 1. stdout: ${output} stderr: ${error}")
endif()
if(NOT output MATCHES "comparison: failed; class: exact")
    message(FATAL_ERROR "imgdiff did not print the expected failure message: ${output}")
endif()

foreach(name IN ITEMS actual.png expected.png mask.png statistics.txt)
    if(NOT EXISTS "${OUTPUT_DIR}/imgdiff/${name}")
        message(FATAL_ERROR "imgdiff did not write ${name} under the requested output directory")
    endif()
endforeach()

message(STATUS "imgdiff artifact case: passed")
