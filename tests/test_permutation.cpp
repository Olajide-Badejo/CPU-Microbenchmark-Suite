// test_permutation.cpp
//
// Proves the single most important invariant in the suite: the chase list is
// one full cycle for every working set size we sweep, under multiple seeds. A
// permutation that split into shorter cycles would keep a small footprint
// resident in L1 regardless of allocation size, silently turning the entire
// latency table into "L1 latency everywhere". We also confirm the detector
// itself rejects a deliberately broken (multi cycle) link array, so a green
// test means the check has teeth.

#include <cstddef>
#include <vector>

#include "pointer_chase.hpp"
#include "test_util.hpp"

int main() {
    // Every swept working set from 4 KB to 512 MB, as node counts.
    const std::size_t stride = bench::kChaseStride;
    for (std::size_t bytes = 4ull * 1024; bytes <= 512ull * 1024 * 1024;
         bytes *= 2) {
        const std::size_t n = bytes / stride;
        for (std::uint64_t seed : {1ull, 42ull, 0x9E3779B97F4A7C15ull}) {
            const auto next = bench::build_cycle_next(n, seed);
            CHECK_EQ(next.size(), n);
            CHECK(bench::is_single_cycle(next));
        }
    }

    // Smallest legal case.
    CHECK(bench::is_single_cycle(bench::build_cycle_next(2, 7)));

    // Negative control: two disjoint 2 cycles over 4 nodes is NOT single cycle.
    std::vector<std::size_t> split = {1, 0, 3, 2};
    CHECK(!bench::is_single_cycle(split));

    // Negative control: a self loop plus a 3 cycle is not single cycle.
    std::vector<std::size_t> selfloop = {0, 2, 3, 1};
    CHECK(!bench::is_single_cycle(selfloop));

    return test::summary();
}
