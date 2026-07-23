cmake_minimum_required(VERSION 3.28)

project(cpu_microbench
    VERSION 1.0.0
    DESCRIPTION "CPU microbenchmark suite for cache latency, STREAM bandwidth, and peak SIMD FLOPS"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# --------------------------------------------------------------------------
# Section 4 rule 4: LTO is forbidden for benchmark targets. Interprocedural
# optimization lets the linker prove a measured loop has no observable effect
# and delete it. We force it off globally and re assert it per benchmark
# target below so a stray toolchain default cannot turn it back on.
# --------------------------------------------------------------------------
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)

if(CMAKE_INTERPROCEDURAL_OPTIMIZATION)
    message(FATAL_ERROR
        "LTO is enabled but is forbidden for this project. "
        "It will delete the benchmark loops being measured.")
endif()

include(cmake/DetectArch.cmake)

# Threads for the STREAM scaling pool.
find_package(Threads REQUIRED)

# --------------------------------------------------------------------------
# add_benchmark(name src...): an executable compiled with the vector flags
# for this architecture, LTO defeated, warnings hot. Every benchmark target
# in the suite is created through this so the flags cannot drift apart.
# --------------------------------------------------------------------------
function(add_benchmark name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_options(${name} PRIVATE
        -O3
        ${ARCH_VECTOR_FLAGS}
        -Wall -Wextra -Wpedantic)
    set_target_properties(${name} PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION OFF)
    target_link_libraries(${name} PRIVATE Threads::Threads)
endfunction()

# The scalar STREAM baseline needs vectorization actively suppressed so the
# "honest scalar" number is genuinely scalar. Same as add_benchmark otherwise.
function(add_scalar_benchmark name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_options(${name} PRIVATE
        -O3
        ${ARCH_BASELINE_FLAGS}
        -fno-tree-vectorize -fno-slp-vectorize
        -Wall -Wextra -Wpedantic)
    set_target_properties(${name} PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION OFF)
    target_link_libraries(${name} PRIVATE Threads::Threads)
endfunction()

enable_testing()

# Subdirectories are added as phases land them.
if(EXISTS ${CMAKE_SOURCE_DIR}/src/CMakeLists.txt)
    add_subdirectory(src)
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/tests/CMakeLists.txt)
    add_subdirectory(tests)
endif()
