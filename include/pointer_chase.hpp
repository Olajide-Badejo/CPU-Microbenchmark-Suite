// pointer_chase.hpp
//
// The chase list construction, factored into a header so the unit test can
// verify the single cycle property against the exact code the benchmark runs.
//
// What is measured (by the .cpp that uses this): dependent load use latency.
// What is deliberately defeated: the hardware stride prefetcher, via a random
// permutation at 64 byte (one cache line) stride, so the next address is never
// predictable and out of order execution cannot overlap the chain.
// What would invalidate every latency number: a permutation that splits into
// more than one cycle. A short cycle stays resident in L1 no matter how large
// the allocation, so the whole table would silently read as L1 latency. The
// test test_permutation.cpp proves one full cycle for every swept size.

#ifndef CPU_MICROBENCH_POINTER_CHASE_HPP
#define CPU_MICROBENCH_POINTER_CHASE_HPP

#include <cstddef>
#include <cstdint>
#include <numeric>
#include <random>
#include <vector>

namespace bench {

// Stride between chase nodes: one cache line, so each hop is a distinct line
// and consecutive lines are never adjacent in memory after shuffling.
inline constexpr std::size_t kChaseStride = 64;

// Build a single cycle visiting order over [0, n). Fisher Yates shuffle of the
// identity, then the cyclic chain order[0] -> order[1] -> ... -> order[n-1] ->
// order[0] is one Hamiltonian cycle by construction. Returned as the `next`
// index array: next[order[i]] = order[(i+1) % n].
inline std::vector<std::size_t> build_cycle_next(std::size_t n,
                                                 std::uint64_t seed) {
    std::vector<std::size_t> order(n);
    std::iota(order.begin(), order.end(), std::size_t{0});

    std::mt19937_64 rng(seed);
    // Fisher Yates: for i from n-1 down to 1, swap i with a uniform j in [0,i].
    for (std::size_t i = n; i-- > 1;) {
        std::uniform_int_distribution<std::size_t> dist(0, i);
        const std::size_t j = dist(rng);
        std::swap(order[i], order[j]);
    }

    std::vector<std::size_t> next(n);
    for (std::size_t i = 0; i < n; ++i) {
        next[order[i]] = order[(i + 1) % n];
    }
    return next;
}

// Verify that `next` is a single cycle covering all n nodes exactly once.
// Used by the unit test and as a cheap internal assertion in the benchmark.
inline bool is_single_cycle(const std::vector<std::size_t>& next) {
    const std::size_t n = next.size();
    if (n == 0) return false;
    std::vector<char> seen(n, 0);
    std::size_t cur = 0;
    for (std::size_t steps = 0; steps < n; ++steps) {
        if (cur >= n) return false;       // out of range link
        if (seen[cur]) return false;      // revisited before covering all
        seen[cur] = 1;
        cur = next[cur];
    }
    // After exactly n hops we must be back at the start and have seen all.
    return cur == 0;
}

}  // namespace bench

#endif  // CPU_MICROBENCH_POINTER_CHASE_HPP
