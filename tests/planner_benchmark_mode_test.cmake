cmake_minimum_required(VERSION 3.28)

if(NOT DEFINED BENCHMARK OR NOT EXISTS "${BENCHMARK}")
    message(FATAL_ERROR "BENCHMARK executable is missing")
endif()

set(root "${CMAKE_CURRENT_BINARY_DIR}/planner_mode_smoke")
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
foreach(expected IN ITEMS
    "\"mode\":\"plan-only\""
    "\"planning_seconds\":"
    "\"file_count\":2"
    "\"directory_count\":0"
    "\"total_bytes\":5"
)
    string(FIND "${output}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Planner-only output missing ${expected}: ${output}")
    endif()
endforeach()
file(GLOB_RECURSE destination_entries LIST_DIRECTORIES TRUE "${root}/destination/*")
if(destination_entries)
    message(FATAL_ERROR "Planner-only mode modified destination: ${destination_entries}")
endif()
execute_process(
    COMMAND "${BENCHMARK}" "${root}/source" "${root}/destination" --plan-only --workers=2
    RESULT_VARIABLE invalid_result
    OUTPUT_QUIET
    ERROR_QUIET
)
if(invalid_result STREQUAL "0")
    message(FATAL_ERROR "--plan-only accepted inapplicable --workers option")
endif()
file(REMOVE_RECURSE "${root}")
