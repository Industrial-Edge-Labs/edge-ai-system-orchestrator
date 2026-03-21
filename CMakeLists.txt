cmake_minimum_required(VERSION 3.18)
project(EdgeAISystemOrchestrator VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(EDGE_ORCHESTRATOR_ENABLE_ZEROMQ "Enable ZeroMQ decision and control-plane integration." OFF)

add_executable(edge_orchestrator
    src/main.cpp
    src/orchestrator/CoreScheduler.cpp
)

target_include_directories(edge_orchestrator PRIVATE src)
target_compile_options(edge_orchestrator PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/O2 /W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O3 -Wall -Wextra>
)
target_compile_definitions(edge_orchestrator PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
    EDGE_ORCHESTRATOR_USE_ZMQ=$<BOOL:${EDGE_ORCHESTRATOR_ENABLE_ZEROMQ}>
)

if (EDGE_ORCHESTRATOR_ENABLE_ZEROMQ)
    find_package(ZeroMQ REQUIRED)
    target_link_libraries(edge_orchestrator PRIVATE zmq)
endif()
