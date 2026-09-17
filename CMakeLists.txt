cmake_minimum_required(VERSION 3.28)

project(VelocityCopy VERSION 0.20.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "VelocityCopy currently targets Windows only.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(velocitycopy_core STATIC
    src/core/copy_engine.cpp
    src/core/destination_browser.cpp
    src/core/destination_catalog.cpp
    src/core/destination_navigation_worker.cpp
    src/core/drop_flow.cpp
    src/core/drop_menu_model.cpp
    src/core/execution_control.cpp
    src/core/ipc_protocol.cpp
    src/core/ipc_transport.cpp
    src/core/job_executor.cpp
    src/core/job_planner.cpp
    src/core/job_planning_worker.cpp
    src/core/job_queue.cpp
    src/core/live_copy_plan.cpp
    src/core/live_copy_plan_export.cpp
    src/core/process_activation.cpp
    src/core/queue_archive.cpp
    src/core/shell_session.cpp
    src/core/storage_profiler.cpp
    src/core/strategy_selector.cpp
    src/core/ui_snapshot.cpp
)

target_include_directories(velocitycopy_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/core/include)

target_compile_definitions(velocitycopy_core PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(velocitycopy_core PUBLIC shell32)

if(MSVC)
    target_compile_options(velocitycopy_core PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

add_executable(VelocityCopyCli src/app/main.cpp)
target_link_libraries(VelocityCopyCli PRIVATE velocitycopy_core)

add_executable(VelocityCopyBenchmark tools/benchmark.cpp)
target_link_libraries(VelocityCopyBenchmark PRIVATE velocitycopy_core)

add_library(VelocityCopyShell SHARED src/shell/explorer_commands.cpp src/shell/shell_strings.rc src/shell/VelocityCopyShell.def)
target_include_directories(VelocityCopyShell PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/shell)
target_link_libraries(VelocityCopyShell PRIVATE velocitycopy_core ole32)
set_target_properties(VelocityCopyShell PROPERTIES OUTPUT_NAME "VelocityCopy.Shell")

if(MSVC)
    target_compile_options(VelocityCopyCli PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_compile_options(VelocityCopyBenchmark PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_compile_options(VelocityCopyShell PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

enable_testing()

function(add_velocitycopy_test target source name)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE velocitycopy_core)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc /utf-8)
    endif()
    add_test(NAME ${name} COMMAND ${target})
endfunction()

add_velocitycopy_test(VelocityCopySmokeTest tests/smoke_test.cpp velocitycopy_smoke)
add_velocitycopy_test(VelocityCopyQueueTest tests/queue_test.cpp velocitycopy_queue)
add_velocitycopy_test(VelocityCopyPlanEditTest tests/plan_edit_test.cpp velocitycopy_plan_edit)
add_velocitycopy_test(VelocityCopyLivePlanTest tests/live_copy_plan_test.cpp velocitycopy_live_plan)
add_velocitycopy_test(VelocityCopyLivePlanStructureTest tests/live_plan_structure_test.cpp velocitycopy_live_plan_structure)
add_velocitycopy_test(VelocityCopyLivePlanSnapshotContractTest tests/live_plan_snapshot_contract_test.cpp velocitycopy_live_plan_snapshot_contract)
add_velocitycopy_test(VelocityCopyParallelExecutorTest tests/job_executor_parallel_test.cpp velocitycopy_parallel_executor)
add_velocitycopy_test(VelocityCopyExecutorAppendTest tests/job_executor_append_test.cpp velocitycopy_executor_append)
add_velocitycopy_test(VelocityCopyExecutionControlTest tests/execution_control_test.cpp velocitycopy_execution_control)
add_velocitycopy_test(VelocityCopySkipTest tests/skip_test.cpp velocitycopy_skip)
add_velocitycopy_test(VelocityCopyExistingDestinationTest tests/existing_destination_test.cpp velocitycopy_existing_destination)
add_velocitycopy_test(VelocityCopyQueueArchiveTest tests/queue_archive_test.cpp velocitycopy_queue_archive)
add_velocitycopy_test(VelocityCopyDropLayoutTest tests/drop_layout_test.cpp velocitycopy_drop_layout)
add_velocitycopy_test(VelocityCopyDropMenuModelTest tests/drop_menu_model_test.cpp velocitycopy_drop_menu_model)
add_velocitycopy_test(VelocityCopyDropFlowTest tests/drop_flow_test.cpp velocitycopy_drop_flow)
add_velocitycopy_test(VelocityCopyDropFlowMultiselectTest tests/drop_flow_multiselect_test.cpp velocitycopy_drop_flow_multiselect)
add_velocitycopy_test(VelocityCopyDestinationCatalogTest tests/destination_catalog_test.cpp velocitycopy_destination_catalog)
add_velocitycopy_test(VelocityCopyDestinationBrowserTest tests/destination_browser_test.cpp velocitycopy_destination_browser)
add_velocitycopy_test(VelocityCopyDestinationNavigationWorkerTest tests/destination_navigation_worker_test.cpp velocitycopy_destination_navigation_worker)
add_velocitycopy_test(VelocityCopyJobPlanningWorkerTest tests/job_planning_worker_test.cpp velocitycopy_job_planning_worker)
add_velocitycopy_test(VelocityCopyStrategyTest tests/strategy_selector_test.cpp velocitycopy_strategy)
add_velocitycopy_test(VelocityCopyShellRequestTest tests/shell_request_test.cpp velocitycopy_shell_request)
add_velocitycopy_test(VelocityCopyIpcTest tests/ipc_test.cpp velocitycopy_ipc)
add_velocitycopy_test(VelocityCopyShellSessionTest tests/shell_session_test.cpp velocitycopy_shell_session)
add_velocitycopy_test(VelocityCopyActivationTest tests/activation_test.cpp velocitycopy_activation)
add_velocitycopy_test(VelocityCopyUiSnapshotTest tests/ui_snapshot_test.cpp velocitycopy_ui_snapshot)
add_velocitycopy_test(VelocityCopyCallbackBoundaryTest tests/copy_engine_callback_test.cpp velocitycopy_callback_boundary)
add_velocitycopy_test(VelocityCopyPauseResumeTest tests/copy_engine_pause_resume_test.cpp velocitycopy_pause_resume)
add_velocitycopy_test(VelocityCopyPlanExecuteTest tests/copy_plan_execute_test.cpp velocitycopy_copy_plan_execute)
add_velocitycopy_test(VelocityCopyPlannerSafetyTest tests/planner_safety_test.cpp velocitycopy_planner_safety)
add_velocitycopy_test(VelocityCopyAppendArchitectureTest tests/copy_append_architecture_test.cpp velocitycopy_append_architecture)
target_compile_definitions(VelocityCopyAppendArchitectureTest PRIVATE VELOCITYCOPY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_velocitycopy_test(VelocityCopySkipArchitectureTest tests/skip_architecture_test.cpp velocitycopy_skip_architecture)
target_compile_definitions(VelocityCopySkipArchitectureTest PRIVATE VELOCITYCOPY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_velocitycopy_test(VelocityCopyQueuePersistenceArchitectureTest tests/queue_persistence_architecture_test.cpp velocitycopy_queue_persistence_architecture)
target_compile_definitions(VelocityCopyQueuePersistenceArchitectureTest PRIVATE VELOCITYCOPY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_velocitycopy_test(VelocityCopyStrategyArchitectureTest tests/copy_strategy_architecture_test.cpp velocitycopy_strategy_architecture)
target_compile_definitions(VelocityCopyStrategyArchitectureTest PRIVATE VELOCITYCOPY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_velocitycopy_test(VelocityCopyCompactUiArchitectureTest tests/compact_ui_architecture_test.cpp velocitycopy_compact_ui_architecture)
target_compile_definitions(VelocityCopyCompactUiArchitectureTest PRIVATE VELOCITYCOPY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_velocitycopy_test(VelocityCopySystemIntegrationArchitectureTest tests/system_integration_architecture_test.cpp velocitycopy_system_integration_architecture)
target_compile_definitions(VelocityCopySystemIntegrationArchitectureTest PRIVATE VELOCITYCOPY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(VelocityCopyShellDllTest tests/shell_dll_test.cpp)
target_link_libraries(VelocityCopyShellDllTest PRIVATE ole32)
add_dependencies(VelocityCopyShellDllTest VelocityCopyShell)
if(MSVC)
    target_compile_options(VelocityCopyShellDllTest PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()
add_test(NAME velocitycopy_shell_dll COMMAND VelocityCopyShellDllTest "$<TARGET_FILE:VelocityCopyShell>")
