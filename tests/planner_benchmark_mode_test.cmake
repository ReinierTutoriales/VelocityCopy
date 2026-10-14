cmake_minimum_required(VERSION 3.28)

if(NOT DEFINED BENCHMARK OR NOT EXISTS "${BENCHMARK}")
    message(FATAL_ERROR "BENCHMARK executable is missing")
endif()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef run_id)
set(root "${CMAKE_CURRENT_BINARY_DIR}/planner_mode_smoke_${run_id}")
file(REMOVE_RECURSE "${root}")
file(MAKE_DIRECTORY "${root}/source" "${root}/destination")
file(WRITE "${root}/source/first.txt" "abc")
file(WRITE "${root}/source/second.txt" "de")

execute_process(
    COMMAND "${BENCHMARK}" "${root}/source" "${root}/destination" --plan-only --json
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Planner-only benchmark failed: ${result}: ${error}")
endif()
string(JSON mode ERROR_VARIABLE json_error GET "${output}" mode)
if(json_error OR NOT mode STREQUAL "plan-only")
    message(FATAL_ERROR "Invalid plan-only JSON mode: ${json_error}: ${output}")
endif()
foreach(field IN ITEMS file_count directory_count total_bytes)
    string(JSON value ERROR_VARIABLE json_error GET "${output}" "${field}")
    if(json_error)
        message(FATAL_ERROR "Missing JSON field ${field}: ${json_error}")
    endif()
    if(field STREQUAL "file_count" AND NOT value STREQUAL "2")
        message(FATAL_ERROR "Unexpected file count: ${value}")
    elseif(field STREQUAL "directory_count" AND NOT value STREQUAL "0")
        message(FATAL_ERROR "Unexpected directory count: ${value}")
    elseif(field STREQUAL "total_bytes" AND NOT value STREQUAL "5")
        message(FATAL_ERROR "Unexpected byte count: ${value}")
    endif()
endforeach()
string(JSON planning_type ERROR_VARIABLE json_error TYPE "${output}" planning_seconds)
if(json_error OR NOT planning_type STREQUAL "NUMBER")
    message(FATAL_ERROR "Missing numeric planning_seconds: ${json_error}: ${output}")
endif()
string(JSON planning_seconds GET "${output}" planning_seconds)
if(planning_seconds LESS 0)
    message(FATAL_ERROR "Negative planning_seconds: ${output}")
endif()
file(GLOB_RECURSE destination_entries LIST_DIRECTORIES TRUE "${root}/destination/*")
if(destination_entries)
    message(FATAL_ERROR "Planner-only mode modified destination: ${destination_entries}")
endif()
execute_process(
    COMMAND "${BENCHMARK}" "${root}/missing-source" "${root}/destination" --plan-only --workers=2
    RESULT_VARIABLE invalid_result
    OUTPUT_QUIET
    ERROR_VARIABLE invalid_error
)
if(invalid_result STREQUAL "0" OR NOT invalid_error MATCHES "--workers is not applicable to --plan-only")
    message(FATAL_ERROR "Invalid options were not rejected before source planning: ${invalid_result}: ${invalid_error}")
endif()
file(REMOVE_RECURSE "${root}")
