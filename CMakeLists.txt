cmake_minimum_required(VERSION 3.14)
project(EdgeAISystemOrchestrator VERSION 1.0.0 LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CUDA_ARCHITECTURES 75 80 86 89)

# Compiler Optimizations for Deterministic Execution
add_compile_options(-O3 -march=native -ffast-math -fno-finite-math-only)

# Find Dependencies
find_package(ZeroMQ REQUIRED)
# In a real environment we would find CUDA, TensorRT, gRPC here
find_package(CUDA REQUIRED)

add_executable(edge_orchestrator
    src/main.cpp
    src/orchestrator/CoreScheduler.cpp
)

target_include_directories(edge_orchestrator PRIVATE src)
target_link_libraries(edge_orchestrator PRIVATE zmq ${CUDA_LIBRARIES})
