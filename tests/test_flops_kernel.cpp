// test_flops_kernel.cpp
//
// Correctness check for the peak FLOPS kernel, and the NEON self test that runs
// under qemu-user during cross compilation. The kernel is a throughput loop,
// but it still computes a well defined value: every accumulator obeys the
// contractive recurrence x <- x * 0.9999 + 1, whose fixed point is
// 1 / (1 - 0.9999) = 10000, reached from any start after enough iterations. So
// after convergence the reduced sum equals chains * lanes * 10000, independent
// of the SIMD width. This makes one assertion valid on both the AVX2 (8 lane)
// and NEON (4 lane) builds, which is exactly what the QEMU self test needs.

#include <cmath>

#include "flops_kernel.hpp"
#include "test_util.hpp"

int main() {
    // Enough iterations to converge: 0.9999^200000 is about 2e-9.
    const float r = bench::flops_kernel_run(200000);
    const double expected =
        static_cast<double>(bench::kFlopsChains) * bench::kFlopsLanes * 10000.0;

    CHECK(std::isfinite(r));
    CHECK_NEAR(static_cast<double>(r), expected, 0.01 * expected);

    // A shorter run must not already be at the fixed point (guards against the
    // loop being optimized to a closed form or skipped): starting near zero,
    // 100 iterations is far from 10000 per lane.
    const float early = bench::flops_kernel_run(100);
    CHECK(static_cast<double>(early) < 0.5 * expected);

    return test::summary();
}
