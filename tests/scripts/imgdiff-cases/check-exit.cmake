# SPDX-FileCopyrightText: 2026 The AriadShot Authors
# SPDX-License-Identifier: GPL-3.0-only

if(NOT DEFINED IMGDIFF OR NOT DEFINED CASE OR NOT DEFINED ACTUAL OR NOT DEFINED EXPECTED)
    message(FATAL_ERROR "imgdiff exit case requires IMGDIFF, CASE, ACTUAL and EXPECTED")
endif()

if(CASE STREQUAL "exact-fail")
    set(arguments --class exact "${ACTUAL}" "${EXPECTED}")
    set(expected_status 1)
    set(expected_message "comparison: failed; class: exact")
elseif(CASE STREQUAL "usage-error")
    set(arguments --class unsupported "${ACTUAL}" "${EXPECTED}")
    set(expected_status 2)
    set(expected_message "usage error")
else()
    message(FATAL_ERROR "unknown imgdiff exit case: ${CASE}")
endif()

execute_process(
    COMMAND "${IMGDIFF}" ${arguments}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
if(NOT result EQUAL expected_status)
    message(FATAL_ERROR "imgdiff returned ${result}, expected ${expected_status}. stdout: ${output} stderr: ${error}")
endif()
if(NOT "${output}${error}" MATCHES "${expected_message}")
    message(FATAL_ERROR "imgdiff output missed '${expected_message}'. stdout: ${output} stderr: ${error}")
endif()

message(STATUS "imgdiff case: passed")
