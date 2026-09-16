cmake_minimum_required(VERSION 3.28)

project(VelocityCopy VERSION 0.3.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "VelocityCopy currently targets Windows only.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(velocitycopy_core STATIC
    src/core/copy_engine.cpp
    src/core/job_executor.cpp
    src/core/job_planner.cpp
    src/core/job_queue.cpp
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

add_executable(VelocityCopy
    src/app/main.cpp
)

target_link_libraries(VelocityCopy PRIVATE velocitycopy_core)

if(MSVC)
    target_compile_options(VelocityCopy PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

enable_testing()

add_executable(VelocityCopySmokeTest
    tests/smoke_test.cpp
)

target_link_libraries(VelocityCopySmokeTest PRIVATE velocitycopy_core)

add_executable(VelocityCopyQueueTest
    tests/queue_test.cpp
)

target_link_libraries(VelocityCopyQueueTest PRIVATE velocitycopy_core)

if(MSVC)
    target_compile_options(VelocityCopySmokeTest PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_compile_options(VelocityCopyQueueTest PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

add_test(NAME velocitycopy_smoke COMMAND VelocityCopySmokeTest)
add_test(NAME velocitycopy_queue COMMAND VelocityCopyQueueTest)
