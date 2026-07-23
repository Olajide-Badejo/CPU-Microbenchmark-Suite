// stream_kernels.hpp
//
// The four STREAM kernels (Copy, Scale, Add, Triad) in three variants:
//   - scalar:  compiled with vectorization suppressed on these functions only,
//              so the "honest scalar" number is genuinely one element per
//              iteration.
//   - vec:     plain loops the compiler is free to auto vectorize at -O3
//              -march=native (what an optimizer gives you for free).
//   - nt:      hand written AVX2 intrinsics using _mm256_stream_ps non temporal
//              stores, which bypass the read for ownership a normal store would
//              trigger, so a write costs one DRAM transfer instead of two. This
//              is the right variant for the DRAM bandwidth test where the
//              arrays are sized past the last level cache.
//
// What is measured with these: sustainable memory bandwidth. What is defeated:
// the write allocate traffic, in the nt variant, via non temporal stores. What
// would invalidate the number: counting bytes wrong (we use the classical
// STREAM convention below), a scalar kernel that secretly vectorized (checked
// by objdump in the engineering notes and implied by the measured gap), or NT
// stores used on a cache resident array (they would bypass the very cache
// being measured, so nt is DRAM only).
//
// STREAM byte convention (McCalpin): elements moved per iteration are
//   Copy 2, Scale 2, Add 3, Triad 3, each times sizeof(float).

#ifndef CPU_MICROBENCH_STREAM_KERNELS_HPP
#define CPU_MICROBENCH_STREAM_KERNELS_HPP

#include <cstddef>

#if defined(__x86_64__)
#include <immintrin.h>
#endif

namespace bench {

// Bytes streamed per element for each kernel, in units of sizeof(float).
inline constexpr int kCopyFactor = 2;
inline constexpr int kScaleFactor = 2;
inline constexpr int kAddFactor = 3;
inline constexpr int kTriadFactor = 3;

// --------------------------------------------------------------------------
// Scalar variant. The optimize attribute suppresses auto and SLP
// vectorization on these functions specifically, leaving the rest of the
// translation unit fully optimized. This is how we get a real scalar baseline
// without splitting the file into a separate target.
// --------------------------------------------------------------------------
#if defined(__GNUC__) && !defined(__clang__)
#define BENCH_SCALAR \
    __attribute__((optimize("no-tree-vectorize", "no-tree-slp-vectorize")))
#else
#define BENCH_SCALAR
#endif

BENCH_SCALAR inline void copy_scalar(float* __restrict c, const float* __restrict a,
                                     std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) c[i] = a[i];
}
BENCH_SCALAR inline void scale_scalar(float* __restrict b, const float* __restrict c,
                                      float q, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) b[i] = q * c[i];
}
BENCH_SCALAR inline void add_scalar(float* __restrict c, const float* __restrict a,
                                    const float* __restrict b, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) c[i] = a[i] + b[i];
}
BENCH_SCALAR inline void triad_scalar(float* __restrict a, const float* __restrict b,
                                      const float* __restrict c, float q,
                                      std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) a[i] = b[i] + q * c[i];
}

// --------------------------------------------------------------------------
// Compiler vectorized variant. Identical source, no suppression, so -O3
// -march=native turns these into AVX2 loops.
// --------------------------------------------------------------------------
inline void copy_vec(float* __restrict c, const float* __restrict a, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) c[i] = a[i];
}
inline void scale_vec(float* __restrict b, const float* __restrict c, float q,
                      std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) b[i] = q * c[i];
}
inline void add_vec(float* __restrict c, const float* __restrict a,
                    const float* __restrict b, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) c[i] = a[i] + b[i];
}
inline void triad_vec(float* __restrict a, const float* __restrict b,
                      const float* __restrict c, float q, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) a[i] = b[i] + q * c[i];
}

// --------------------------------------------------------------------------
// AVX2 intrinsic variant with non temporal stores. Requires 32 byte aligned
// destinations (our allocator gives 64). Processes 8 floats per iteration; a
// scalar tail handles any remainder. A store fence is issued by the caller
// after the timed region, not per element.
// --------------------------------------------------------------------------
#if defined(__x86_64__) && defined(__AVX2__)
inline void copy_nt(float* __restrict c, const float* __restrict a, std::size_t n) {
    std::size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        __m256 v = _mm256_load_ps(a + i);
        _mm256_stream_ps(c + i, v);
    }
    for (; i < n; ++i) c[i] = a[i];
}
inline void scale_nt(float* __restrict b, const float* __restrict c, float q,
                     std::size_t n) {
    const __m256 vq = _mm256_set1_ps(q);
    std::size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        __m256 v = _mm256_mul_ps(vq, _mm256_load_ps(c + i));
        _mm256_stream_ps(b + i, v);
    }
    for (; i < n; ++i) b[i] = q * c[i];
}
inline void add_nt(float* __restrict c, const float* __restrict a,
                   const float* __restrict b, std::size_t n) {
    std::size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        __m256 v = _mm256_add_ps(_mm256_load_ps(a + i), _mm256_load_ps(b + i));
        _mm256_stream_ps(c + i, v);
    }
    for (; i < n; ++i) c[i] = a[i] + b[i];
}
inline void triad_nt(float* __restrict a, const float* __restrict b,
                     const float* __restrict c, float q, std::size_t n) {
    const __m256 vq = _mm256_set1_ps(q);
    std::size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        __m256 v = _mm256_fmadd_ps(vq, _mm256_load_ps(c + i), _mm256_load_ps(b + i));
        _mm256_stream_ps(a + i, v);
    }
    for (; i < n; ++i) a[i] = b[i] + q * c[i];
}
inline void store_fence() { _mm_sfence(); }
#else
// Fallback so the file compiles on non AVX2 targets (for example the AArch64
// port, which supplies its own NEON path elsewhere). These just call the
// vectorized variant and a no op fence.
inline void copy_nt(float* c, const float* a, std::size_t n) { copy_vec(c, a, n); }
inline void scale_nt(float* b, const float* c, float q, std::size_t n) {
    scale_vec(b, c, q, n);
}
inline void add_nt(float* c, const float* a, const float* b, std::size_t n) {
    add_vec(c, a, b, n);
}
inline void triad_nt(float* a, const float* b, const float* c, float q,
                     std::size_t n) {
    triad_vec(a, b, c, q, n);
}
inline void store_fence() {}
#endif

}  // namespace bench

#endif  // CPU_MICROBENCH_STREAM_KERNELS_HPP
