# Unit and integration tests. Plain executables whose exit code CTest reads.
# LTO is off here too (inherited), and warnings stay hot.

add_executable(test_permutation test_permutation.cpp)
target_include_directories(test_permutation PRIVATE
    ${CMAKE_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(test_permutation PRIVATE -O2 -Wall -Wextra -Wpedantic)
add_test(NAME permutation_single_cycle COMMAND test_permutation)

add_executable(test_topology test_topology.cpp)
target_include_directories(test_topology PRIVATE
    ${CMAKE_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(test_topology PRIVATE -O2 -Wall -Wextra -Wpedantic)
target_compile_definitions(test_topology PRIVATE
    FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_test(NAME topology_parser COMMAND test_topology)

# Built with the native vector flags so the real AVX2 intrinsic kernels are
# exercised, not the scalar fallback.
add_executable(test_stream_verify test_stream_verify.cpp)
target_include_directories(test_stream_verify PRIVATE
    ${CMAKE_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(test_stream_verify PRIVATE
    -O3 ${ARCH_VECTOR_FLAGS} -Wall -Wextra -Wpedantic)
add_test(NAME stream_numeric_verify COMMAND test_stream_verify)

# Python integration test: plateau detector on a synthetic curve.
find_program(PYTHON3 python3)
if(PYTHON3)
    add_test(NAME plateau_detect
        COMMAND ${PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/python/test_plateau.py)
endif()
