cmake_minimum_required(VERSION 3.18)
project(GPU-Virtual-Memory-Runtime LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)

option(USE_GPU_SIMULATOR "Use GPU simulator mode (no real GPU required)" ON)
option(ENABLE_TESTS "Build unit tests" ON)
option(ENABLE_BENCHMARKS "Build benchmark applications" ON)
option(ENABLE_EXAMPLES "Build example applications" ON)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(CUDA 11.0 REQUIRED)

if(ENABLE_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG release-1.12.1
    )
    FetchContent_MakeAvailable(googletest)
    enable_testing()
endif()

add_library(gpu_vm_core STATIC
    src/vm/Common.h
    src/vm/PageTable.h
    src/vm/PageTable.cpp
    src/vm/PageAllocator.h
    src/vm/PageAllocator.cpp
    src/vm/TLB.h
    src/vm/TLB.cpp
    src/vm/Policies.h
    src/vm/Policies.cpp
    src/vm/MigrationManager.h
    src/vm/MigrationManager.cpp
    src/vm/VirtualMemoryManager.h
    src/vm/VirtualMemoryManager.cpp
)

target_include_directories(gpu_vm_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CUDA_INCLUDE_DIRS}
)

target_compile_features(gpu_vm_core PRIVATE cxx_std_17)

if(CUDA_FOUND)
    target_link_libraries(gpu_vm_core PUBLIC ${CUDA_LIBRARIES})
endif()

add_library(gpu_vm_kernels STATIC
    src/device/device_helpers.cu
)

set_target_properties(gpu_vm_kernels PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
)

target_include_directories(gpu_vm_kernels PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CUDA_INCLUDE_DIRS}
)

target_link_libraries(gpu_vm_kernels PUBLIC ${CUDA_LIBRARIES})

if(ENABLE_BENCHMARKS)
    add_executable(benchmark_app src/bench/benchmark_app.cpp)
    
    target_include_directories(benchmark_app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    
    target_link_libraries(benchmark_app PRIVATE
        gpu_vm_core
    )
    
    target_compile_features(benchmark_app PRIVATE cxx_std_17)
endif()

if(ENABLE_EXAMPLES)
    # N-Body Example
    add_executable(nbody_gpu_vm src/examples/nbody_gpu_vm.cpp)
    
    target_include_directories(nbody_gpu_vm PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    
    target_link_libraries(nbody_gpu_vm PRIVATE
        gpu_vm_core
    )
    
    target_compile_features(nbody_gpu_vm PRIVATE cxx_std_17)
    
    add_executable(video_pipeline_sim src/examples/video_pipeline_sim.cpp)
    
    target_include_directories(video_pipeline_sim PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    
    target_link_libraries(video_pipeline_sim PRIVATE
        gpu_vm_core
    )
    
    target_compile_features(video_pipeline_sim PRIVATE cxx_std_17)
endif()

if(ENABLE_TESTS)
    add_executable(vm_tests
        tests/vm_tests.cpp
    )
    
    target_include_directories(vm_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    
    target_link_libraries(vm_tests PRIVATE
        gpu_vm_core
        gtest
        gtest_main
    )
    
    target_compile_features(vm_tests PRIVATE cxx_std_17)
    
    gtest_discover_tests(vm_tests)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(gpu_vm_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(gpu_vm_kernels PRIVATE -Wall -Wextra)
endif()

if(MSVC)
    target_compile_options(gpu_vm_core PRIVATE /W4)
    target_compile_options(gpu_vm_kernels PRIVATE /W4)
endif()

install(TARGETS gpu_vm_core
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

if(ENABLE_BENCHMARKS)
    install(TARGETS benchmark_app DESTINATION bin)
endif()

if(ENABLE_EXAMPLES)
    install(TARGETS nbody_gpu_vm video_pipeline_sim DESTINATION bin)
endif()

message(STATUS "GPU Virtual Memory Runtime Configuration:")
message(STATUS "  GPU Simulator Mode: ${USE_GPU_SIMULATOR}")
message(STATUS "  Build Tests: ${ENABLE_TESTS}")
message(STATUS "  Build Benchmarks: ${ENABLE_BENCHMARKS}")
message(STATUS "  Build Examples: ${ENABLE_EXAMPLES}")
message(STATUS "  CUDA Found: ${CUDA_FOUND}")
message(STATUS "  CUDA Version: ${CUDA_VERSION}")
