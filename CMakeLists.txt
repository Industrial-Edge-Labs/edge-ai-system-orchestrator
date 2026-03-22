cmake_minimum_required(VERSION 3.18)
project(EdgeAISystemOrchestrator VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(EDGE_ORCHESTRATOR_ENABLE_ZEROMQ "Enable ZeroMQ decision and control-plane integration." OFF)
option(EDGE_ORCHESTRATOR_ENABLE_TESTS "Build local runtime and synthetic-input tests." ON)

add_library(orchestrator_core
    src/orchestrator/CoreScheduler.cpp
    src/orchestrator/RuntimeConfig.cpp
    src/orchestrator/SyntheticInputs.cpp
)

target_include_directories(orchestrator_core PUBLIC src)
target_compile_options(orchestrator_core PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/O2 /W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O3 -Wall -Wextra>
)
target_compile_definitions(orchestrator_core PUBLIC
    $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
    EDGE_ORCHESTRATOR_USE_ZMQ=$<BOOL:${EDGE_ORCHESTRATOR_ENABLE_ZEROMQ}>
)

add_executable(edge_orchestrator
    src/main.cpp
)

target_include_directories(edge_orchestrator PRIVATE src)
target_link_libraries(edge_orchestrator PRIVATE orchestrator_core)

if (EDGE_ORCHESTRATOR_ENABLE_ZEROMQ)
    find_package(ZeroMQ REQUIRED)
    target_link_libraries(orchestrator_core PUBLIC zmq)
endif()

if (EDGE_ORCHESTRATOR_ENABLE_TESTS)
    enable_testing()
    add_executable(orchestrator_tests
        tests/orchestrator_tests.cpp
        src/orchestrator/RuntimeConfig.cpp
        src/orchestrator/SyntheticInputs.cpp
    )
    target_include_directories(orchestrator_tests PRIVATE src)
    target_compile_definitions(orchestrator_tests PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
        EDGE_ORCHESTRATOR_USE_ZMQ=0
    )
    add_test(NAME orchestrator_tests COMMAND orchestrator_tests)
endif()
