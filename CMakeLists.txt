cmake_minimum_required(VERSION 3.28)

project(VelocityCopy VERSION 0.14.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "VelocityCopy currently targets Windows only.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(velocitycopy_core STATIC
    src/core/copy_engine.cpp
    src/core/destination_catalog.cpp
    src/core/drop_menu_model.cpp
    src/core/ipc_protocol.cpp
    src/core/ipc_transport.cpp
    src/core/job_executor.cpp
    src/core/job_planner.cpp
    src/core/job_queue.cpp
    src/core/process_activation.cpp
    src/core/shell_session.cpp
    src/core/storage_profiler.cpp
    src/core/strategy_selector.cpp
    src/core/ui_snapshot.cpp
)

target_include_directories(velocitycopy_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core/include
)

target_compile_definitions(velocitycopy_core
    PUBLIC
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0A00
        WINVER=0x0A00
)

if(MSVC)
    target_compile_options(velocitycopy_core PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

add_executable(VelocityCopy src/app/main.cpp)
target_link_libraries(VelocityCopy PRIVATE velocitycopy_core)

add_executable(VelocityCopyBenchmark tools/benchmark.cpp)
target_link_libraries(VelocityCopyBenchmark PRIVATE velocitycopy_core)

add_library(VelocityCopyShell SHARED
    src/shell/explorer_commands.cpp
    src/shell/shell_strings.rc
    src/shell/VelocityCopyShell.def
)
target_include_directories(VelocityCopyShell PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/shell)
target_link_libraries(VelocityCopyShell PRIVATE velocitycopy_core ole32)
set_target_properties(VelocityCopyShell PROPERTIES OUTPUT_NAME "VelocityCopy.Shell")

if(MSVC)
    target_compile_options(VelocityCopy PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_compile_options(VelocityCopyBenchmark PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_compile_options(VelocityCopyShell PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

enable_testing()

add_executable(VelocityCopySmokeTest tests/smoke_test.cpp)
target_link_libraries(VelocityCopySmokeTest PRIVATE velocitycopy_core)

add_executable(VelocityCopyQueueTest tests/queue_test.cpp)
target_link_libraries(VelocityCopyQueueTest PRIVATE velocitycopy_core)

add_executable(VelocityCopyPlanEditTest tests/plan_edit_test.cpp)
target_link_libraries(VelocityCopyPlanEditTest PRIVATE velocitycopy_core)

add_executable(VelocityCopyDropLayoutTest tests/drop_layout_test.cpp)
target_link_libraries(VelocityCopyDropLayoutTest PRIVATE velocitycopy_core)

add_executable(VelocityCopyDropMenuModelTest tests/drop_menu_model_test.cpp)
target_link_libraries(VelocityCopyDropMenuModelTest PRIVATE velocitycopy_core)

add_executable(VelocityCopyDestinationCatalogTest tests/destination_catalog_test.cpp)
target_link_libraries(VelocityCopyDestinationCatalogTest PRIVATE velocitycopy_core shell32)

add_executable(VelocityCopyStrategyTest tests/strategy_selector_test.cpp)
target_link_libraries(VelocityCopyStrategyTest PRIVATE velocitycopy_core)

add_executable(VelocityCopyShellRequestTest tests/shell_request_test.cpp)
target_link_libraries(VelocityCopyShellRequestTest PRIVATE velocitycopy_core)

add_executable(VelocityCopyIpcTest tests/ipc_test.cpp)
target_link_libraries(VelocityCopyIpcTest PRIVATE velocitycopy_core)

add_executable(VelocityCopyShellSessionTest tests/shell_session_test.cpp)
target_link_libraries(VelocityCopyShellSessionTest PRIVATE velocitycopy_core)

add_executable(VelocityCopyActivationTest tests/activation_test.cpp)
target_link_libraries(VelocityCopyActivationTest PRIVATE velocitycopy_core)

add_executable(VelocityCopyUiSnapshotTest tests/ui_snapshot_test.cpp)
target_link_libraries(VelocityCopyUiSnapshotTest PRIVATE velocitycopy_core)

add_executable(VelocityCopyShellDllTest tests/shell_dll_test.cpp)
target_link_libraries(VelocityCopyShellDllTest PRIVATE ole32)
add_dependencies(VelocityCopyShellDllTest VelocityCopyShell)

if(MSVC)
    target_compile_options(VelocityCopySmokeTest PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_compile_options(VelocityCopyQueueTest PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_compile_options(VelocityCopyPlanEditTest PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_compile_options(VelocityCopyDropLayoutTest PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_compile_options(VelocityCopyDropMenuModelTest PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_compile_options(VelocityCopyDestinationCatalogTest PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_compile_options(VelocityCopyStrategyTest PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_compile_options(VelocityCopyShellRequestTest PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_compile_options(VelocityCopyIpcTest PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_compile_options(VelocityCopyShellSessionTest PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_compile_options(VelocityCopyActivationTest PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_compile_options(VelocityCopyUiSnapshotTest PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_compile_options(VelocityCopyShellDllTest PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

add_test(NAME velocitycopy_smoke COMMAND VelocityCopySmokeTest)
add_test(NAME velocitycopy_queue COMMAND VelocityCopyQueueTest)
add_test(NAME velocitycopy_plan_edit COMMAND VelocityCopyPlanEditTest)
add_test(NAME velocitycopy_drop_layout COMMAND VelocityCopyDropLayoutTest)
add_test(NAME velocitycopy_drop_menu_model COMMAND VelocityCopyDropMenuModelTest)
add_test(NAME velocitycopy_destination_catalog COMMAND VelocityCopyDestinationCatalogTest)
add_test(NAME velocitycopy_strategy COMMAND VelocityCopyStrategyTest)
add_test(NAME velocitycopy_shell_request COMMAND VelocityCopyShellRequestTest)
add_test(NAME velocitycopy_ipc COMMAND VelocityCopyIpcTest)
add_test(NAME velocitycopy_shell_session COMMAND VelocityCopyShellSessionTest)
add_test(NAME velocitycopy_activation COMMAND VelocityCopyActivationTest)
add_test(NAME velocitycopy_ui_snapshot COMMAND VelocityCopyUiSnapshotTest)
add_test(NAME velocitycopy_shell_dll COMMAND VelocityCopyShellDllTest "$<TARGET_FILE:VelocityCopyShell>")
