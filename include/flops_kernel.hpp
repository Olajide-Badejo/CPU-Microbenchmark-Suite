// flops_kernel.hpp
//
// Measures: peak single precision FMA throughput. The kernel runs ten
// independent fused multiply add accumulator chains. Ten because the FMA units
// have a latency of four to five cycles across two pipes, so at least eight to
// ten in flight chains are needed to keep both pipes issuing every cycle; fewer
// chains leave the pipes stalling on the latency of a single dependent chain.
//
// Deliberately arranged: the accumulators are independent (no chain depends on
// another), the multiplier is non unity so the compiler cannot strength reduce
// the recurrence into an add, and the recurrence is contractive (multiplier
// just under one) so values stay finite and normal rather than drifting to
// infinity, which would risk a microcode slow path on some hardware. The final
// reduction is returned so the caller's do_not_optimize keeps the whole thing
// from being deleted as dead.
//
// Invalidates the number: fewer live chains than the pipes need (throughput
// underreported), or the compiler proving the loop dead (returns absurdly high
// GFLOPS). Both are guarded here and by comparison to the computed ceiling.

#ifndef CPU_MICROBENCH_FLOPS_KERNEL_HPP
#define CPU_MICROBENCH_FLOPS_KERNEL_HPP

#include <cstdint>

#if defined(__x86_64__)
#include <immintrin.h>
#elif defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace bench {

inline constexpr int kFlopsChains = 10;

#if defined(__x86_64__) && defined(__AVX2__)
inline constexpr int kFlopsLanes = 8;  // 256 bit / 32 bit float
// Two FLOPs per FMA (one multiply, one add) times the SIMD width times chains.
inline constexpr double kFlopsPerIter =
    static_cast<double>(kFlopsChains) * kFlopsLanes * 2.0;

inline float flops_kernel_run(std::uint64_t iters) {
    __m256 a0 = _mm256_set1_ps(0.01f), a1 = _mm256_set1_ps(0.02f);
    __m256 a2 = _mm256_set1_ps(0.03f), a3 = _mm256_set1_ps(0.04f);
    __m256 a4 = _mm256_set1_ps(0.05f), a5 = _mm256_set1_ps(0.06f);
    __m256 a6 = _mm256_set1_ps(0.07f), a7 = _mm256_set1_ps(0.08f);
    __m256 a8 = _mm256_set1_ps(0.09f), a9 = _mm256_set1_ps(0.10f);
    const __m256 m = _mm256_set1_ps(0.9999f);  // non unity, contractive
    const __m256 c = _mm256_set1_ps(1.0f);
    for (std::uint64_t i = 0; i < iters; ++i) {
        a0 = _mm256_fmadd_ps(a0, m, c);
        a1 = _mm256_fmadd_ps(a1, m, c);
        a2 = _mm256_fmadd_ps(a2, m, c);
        a3 = _mm256_fmadd_ps(a3, m, c);
        a4 = _mm256_fmadd_ps(a4, m, c);
        a5 = _mm256_fmadd_ps(a5, m, c);
        a6 = _mm256_fmadd_ps(a6, m, c);
        a7 = _mm256_fmadd_ps(a7, m, c);
        a8 = _mm256_fmadd_ps(a8, m, c);
        a9 = _mm256_fmadd_ps(a9, m, c);
    }
    __m256 s = _mm256_add_ps(_mm256_add_ps(_mm256_add_ps(a0, a1),
                                           _mm256_add_ps(a2, a3)),
                             _mm256_add_ps(_mm256_add_ps(a4, a5),
                                           _mm256_add_ps(a6, a7)));
    s = _mm256_add_ps(s, _mm256_add_ps(a8, a9));
    float buf[8];
    _mm256_storeu_ps(buf, s);
    float r = 0.0f;
    for (int k = 0; k < 8; ++k) r += buf[k];
    return r;
}

#elif defined(__ARM_NEON)
inline constexpr int kFlopsLanes = 4;  // 128 bit / 32 bit float
inline constexpr double kFlopsPerIter =
    static_cast<double>(kFlopsChains) * kFlopsLanes * 2.0;

inline float flops_kernel_run(std::uint64_t iters) {
    float32x4_t a0 = vdupq_n_f32(0.01f), a1 = vdupq_n_f32(0.02f);
    float32x4_t a2 = vdupq_n_f32(0.03f), a3 = vdupq_n_f32(0.04f);
    float32x4_t a4 = vdupq_n_f32(0.05f), a5 = vdupq_n_f32(0.06f);
    float32x4_t a6 = vdupq_n_f32(0.07f), a7 = vdupq_n_f32(0.08f);
    float32x4_t a8 = vdupq_n_f32(0.09f), a9 = vdupq_n_f32(0.10f);
    const float32x4_t m = vdupq_n_f32(0.9999f);
    const float32x4_t c = vdupq_n_f32(1.0f);
    for (std::uint64_t i = 0; i < iters; ++i) {
        a0 = vfmaq_f32(c, a0, m);
        a1 = vfmaq_f32(c, a1, m);
        a2 = vfmaq_f32(c, a2, m);
        a3 = vfmaq_f32(c, a3, m);
        a4 = vfmaq_f32(c, a4, m);
        a5 = vfmaq_f32(c, a5, m);
        a6 = vfmaq_f32(c, a6, m);
        a7 = vfmaq_f32(c, a7, m);
        a8 = vfmaq_f32(c, a8, m);
        a9 = vfmaq_f32(c, a9, m);
    }
    float32x4_t s = vaddq_f32(vaddq_f32(vaddq_f32(a0, a1), vaddq_f32(a2, a3)),
                              vaddq_f32(vaddq_f32(a4, a5), vaddq_f32(a6, a7)));
    s = vaddq_f32(s, vaddq_f32(a8, a9));
    return vgetq_lane_f32(s, 0) + vgetq_lane_f32(s, 1) +
           vgetq_lane_f32(s, 2) + vgetq_lane_f32(s, 3);
}

#else
inline constexpr int kFlopsLanes = 1;
inline constexpr double kFlopsPerIter =
    static_cast<double>(kFlopsChains) * kFlopsLanes * 2.0;

inline float flops_kernel_run(std::uint64_t iters) {
    float a[kFlopsChains];
    for (int k = 0; k < kFlopsChains; ++k) a[k] = 0.01f * (k + 1);
    const float m = 0.9999f, c = 1.0f;
    for (std::uint64_t i = 0; i < iters; ++i)
        for (int k = 0; k < kFlopsChains; ++k) a[k] = a[k] * m + c;
    float r = 0.0f;
    for (int k = 0; k < kFlopsChains; ++k) r += a[k];
    return r;
}
#endif

}  // namespace bench

#endif  // CPU_MICROBENCH_FLOPS_KERNEL_HPP
