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
