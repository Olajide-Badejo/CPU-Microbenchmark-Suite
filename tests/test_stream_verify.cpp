// test_stream_verify.cpp
//
// Confirms every STREAM kernel, in all three variants, computes the correct
// result. A bandwidth number is only meaningful if the kernel actually did the
// arithmetic it claims, so we run each kernel on a small array and compare
// against a hand computed reference. The three variants must agree with each
// other and with the reference (float tolerance covers the fused multiply add
// in triad_nt).
//
// Built with the native vector flags so the real AVX2 intrinsic path is the
// one under test, not the scalar fallback. Buffers are 64 byte aligned because
// _mm256_stream_ps and _mm256_load_ps fault on an unaligned address.

#include <cstddef>
#include <cstdlib>

#include "bench_utils.hpp"
#include "stream_kernels.hpp"
#include "test_util.hpp"

namespace {

constexpr std::size_t N = 1024;  // multiple of 8 for the intrinsic path
constexpr float Q = 3.0f;

// Small aligned float buffer that frees itself.
struct AlignedBuf {
    explicit AlignedBuf(float v) {
        p = static_cast<float*>(bench::aligned_alloc_bytes(64, N * sizeof(float)));
        for (std::size_t i = 0; i < N; ++i) p[i] = v;
    }
    ~AlignedBuf() { std::free(p); }
    float* p;
};

}  // namespace

int main() {
    // Copy: c = a
    for (auto copy : {bench::copy_scalar, bench::copy_vec, bench::copy_nt}) {
        AlignedBuf a(1.5f), c(0.0f);
        copy(c.p, a.p, N);
        bench::store_fence();
        CHECK_EQ(c.p[0], 1.5f);
        CHECK_EQ(c.p[7], 1.5f);
        CHECK_EQ(c.p[8], 1.5f);  // just past the first vector
        CHECK_EQ(c.p[N - 1], 1.5f);
    }

    // Scale: b = q * c
    for (auto scale : {bench::scale_scalar, bench::scale_vec, bench::scale_nt}) {
        AlignedBuf c(2.0f), b(0.0f);
        scale(b.p, c.p, Q, N);
        bench::store_fence();
        CHECK_NEAR(b.p[0], 6.0f, 1e-5f);
        CHECK_NEAR(b.p[N - 1], 6.0f, 1e-5f);
    }

    // Add: c = a + b
    for (auto add : {bench::add_scalar, bench::add_vec, bench::add_nt}) {
        AlignedBuf a(1.0f), b(2.5f), c(0.0f);
        add(c.p, a.p, b.p, N);
        bench::store_fence();
        CHECK_NEAR(c.p[0], 3.5f, 1e-5f);
        CHECK_NEAR(c.p[N - 1], 3.5f, 1e-5f);
    }

    // Triad: a = b + q * c
    for (auto triad : {bench::triad_scalar, bench::triad_vec, bench::triad_nt}) {
        AlignedBuf b(1.0f), c(2.0f), a(0.0f);
        triad(a.p, b.p, c.p, Q, N);
        bench::store_fence();
        CHECK_NEAR(a.p[0], 7.0f, 1e-4f);  // 1 + 3*2
        CHECK_NEAR(a.p[N - 1], 7.0f, 1e-4f);
    }

    return test::summary();
}
