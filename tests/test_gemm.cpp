// test_gemm.cpp
//
// Confirms the blocked GEMM computes the same result as a plain triple loop
// reference, for several tile sizes including tiles that do not divide the
// matrix dimensions evenly (the boundary handling is where blocking bugs hide).
// A throughput number for a tile is meaningless if that tile computes the wrong
// matrix, so this gates the GEMM validator.

#include <cstdlib>
#include <vector>

#include "gemm.hpp"
#include "test_util.hpp"

int main() {
    const int M = 37, N = 41, K = 29;  // deliberately not multiples of any tile
    std::vector<float> A(static_cast<std::size_t>(M) * K);
    std::vector<float> B(static_cast<std::size_t>(K) * N);
    std::vector<float> ref(static_cast<std::size_t>(M) * N);
    std::vector<float> got(static_cast<std::size_t>(M) * N);

    // Cast to int before subtracting: i % N is size_t (unsigned), so a bare
    // subtraction underflows to a huge value for the small residues.
    for (std::size_t i = 0; i < A.size(); ++i)
        A[i] = static_cast<float>(static_cast<int>(i % 11) - 5) * 0.25f;
    for (std::size_t i = 0; i < B.size(); ++i)
        B[i] = static_cast<float>(static_cast<int>(i % 9) - 4) * 0.5f;

    bench::gemm_reference(A.data(), B.data(), ref.data(), M, N, K);

    const int tiles[][3] = {
        {8, 8, 8}, {16, 32, 16}, {64, 64, 64}, {M, K, N}, {7, 5, 13}};
    for (const auto& t : tiles) {
        bench::gemm_blocked(A.data(), B.data(), got.data(), M, N, K,
                            t[0], t[1], t[2]);
        for (std::size_t i = 0; i < ref.size(); ++i) {
            CHECK_NEAR(got[i], ref[i], 1e-3f);
        }
    }

    return test::summary();
}
