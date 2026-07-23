// gemm.hpp
//
// A naive blocked single precision GEMM, C = A * B, row major. Blocked by
// (Mc, Kc, Nc) so a chosen tile of A stays in L2 and a tile of B in L3 while
// the innermost loop streams contiguously over N and auto vectorizes. This is
// deliberately not a high performance micro kernel: the point is not to beat a
// tuned BLAS, it is to measure which tile size runs fastest and check that
// against the BLIS analytical prediction (Phase 5). Correctness is verified in
// test_gemm.cpp against a plain triple loop.
//
// Loop order ic, pc, jc then i, p, j: with j innermost and contiguous in both
// B and C, the compiler vectorizes the update C[i,j] += a * B[p,j] cleanly.

#ifndef CPU_MICROBENCH_GEMM_HPP
#define CPU_MICROBENCH_GEMM_HPP

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace bench {

// C must have room for M*N floats; it is zeroed then accumulated into.
inline void gemm_blocked(const float* __restrict A, const float* __restrict B,
                         float* __restrict C, int M, int N, int K,
                         int Mc, int Kc, int Nc) {
    std::memset(C, 0, static_cast<std::size_t>(M) * N * sizeof(float));
    for (int ic = 0; ic < M; ic += Mc) {
        const int imax = std::min(ic + Mc, M);
        for (int pc = 0; pc < K; pc += Kc) {
            const int pmax = std::min(pc + Kc, K);
            for (int jc = 0; jc < N; jc += Nc) {
                const int jmax = std::min(jc + Nc, N);
                for (int i = ic; i < imax; ++i) {
                    float* __restrict crow = C + static_cast<std::size_t>(i) * N;
                    const float* __restrict arow =
                        A + static_cast<std::size_t>(i) * K;
                    for (int p = pc; p < pmax; ++p) {
                        const float a = arow[p];
                        const float* __restrict brow =
                            B + static_cast<std::size_t>(p) * N;
                        for (int j = jc; j < jmax; ++j) {
                            crow[j] += a * brow[j];
                        }
                    }
                }
            }
        }
    }
}

// Reference triple loop, for the correctness test only.
inline void gemm_reference(const float* A, const float* B, float* C, int M,
                           int N, int K) {
    for (int i = 0; i < M; ++i) {
        for (int j = 0; j < N; ++j) {
            float acc = 0.0f;
            for (int k = 0; k < K; ++k) {
                acc += A[static_cast<std::size_t>(i) * K + k] *
                       B[static_cast<std::size_t>(k) * N + j];
            }
            C[static_cast<std::size_t>(i) * N + j] = acc;
        }
    }
}

}  // namespace bench

#endif  // CPU_MICROBENCH_GEMM_HPP
