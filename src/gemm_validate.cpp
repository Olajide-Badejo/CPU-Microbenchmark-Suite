// gemm_validate.cpp
//
// Measures: the throughput of a naive blocked FP32 GEMM across a grid of tile
// sizes, so the empirical best tile can be compared against the BLIS analytical
// prediction (tile_predict.py). Emits the measured cache geometry (sizes,
// associativity, line) so the analytical model and the empirical sweep are
// driven by the same numbers.
//
// Deliberately single core and pinned to one P core: cache blocking behavior is
// cleanest without inter core sharing noise. The kernel is intentionally naive
// (blocked triple loop, auto vectorized inner) so the tile size, not a hand
// tuned micro kernel, is what moves the number. A tile far from the prediction
// that wins the sweep is a reported finding, not a hidden failure.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "bench_utils.hpp"
#include "gemm.hpp"
#include "progress.hpp"
#include "topology.hpp"

namespace {

struct Options {
    int n = 1536;   // square matrices M = N = K = n
    int trials = 3;
    std::string out_path;
    bool mini = false;
};

Options parse_args(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            return (i + 1 < argc) ? argv[++i] : std::string();
        };
        if (a == "--out") o.out_path = next();
        else if (a == "--n") o.n = std::stoi(next());
        else if (a == "--trials") o.trials = std::stoi(next());
        else if (a == "--mini") {
            o.mini = true;
            o.n = 512;
            o.trials = 2;
        } else if (a == "--help") {
            std::printf("gemm_validate [--out FILE] [--n N] [--trials N] [--mini]\n");
            std::exit(0);
        }
    }
    return o;
}

}  // namespace

int main(int argc, char** argv) {
    const Options opt = parse_args(argc, argv);
    const int n = opt.n;
    const bench::Topology topo = bench::read_topology();

    const int p_core = bench::default_p_core(topo);
    bench::pin_to_cpu(p_core);
    const double clock_ghz = bench::measure_clock_ghz_calibrated();

    const std::size_t elems = static_cast<std::size_t>(n) * n;
    auto* A = static_cast<float*>(bench::aligned_alloc_bytes(64, elems * sizeof(float)));
    auto* B = static_cast<float*>(bench::aligned_alloc_bytes(64, elems * sizeof(float)));
    auto* C = static_cast<float*>(bench::aligned_alloc_bytes(64, elems * sizeof(float)));
    for (std::size_t i = 0; i < elems; ++i) {
        // Cast to int before subtracting: i % N is unsigned and would underflow.
        A[i] = static_cast<float>(static_cast<int>(i % 13) - 6) * 0.1f;
        B[i] = static_cast<float>(static_cast<int>(i % 7) - 3) * 0.1f;
    }

    // Tile grid. Mc and Kc are the interesting dimensions for L2 and L1 reuse;
    // Nc is held at the full width (the B panel spans L3). Grid values chosen to
    // straddle the plausible BLIS prediction for this machine.
    std::vector<int> mc_values = {96, 192, 384, 768};
    std::vector<int> kc_values = {32, 64, 128, 256, 384};
    if (opt.mini) {
        mc_values = {96, 192};
        kc_values = {64, 128};
    }
    const int nc = n;

    const double flops = 2.0 * n * n * n;
    bench::Progress bar("gemm sweep",
                        static_cast<int>(mc_values.size() * kc_values.size()));

    bench::JsonWriter jw;
    jw.field("benchmark", std::string("gemm"));
    jw.field("clock_ghz_measured", clock_ghz);
    jw.field("matrix_n", static_cast<long long>(n));
    jw.field("nc_fixed", static_cast<long long>(nc));
    jw.field("dtype_bytes", static_cast<long long>(sizeof(float)));
    // Cache geometry for the analytical model.
    jw.field("l1d_bytes", static_cast<long long>(topo.l1d_bytes));
    jw.field("l1d_ways", static_cast<long long>(topo.l1d_ways));
    jw.field("l2_bytes", static_cast<long long>(topo.l2_bytes));
    jw.field("l2_ways", static_cast<long long>(topo.l2_ways));
    jw.field("l3_bytes", static_cast<long long>(topo.l3_bytes));
    jw.field("l3_ways", static_cast<long long>(topo.l3_ways));
    jw.field("line_bytes", static_cast<long long>(topo.line_bytes));
    jw.begin_array("sweep");

    double best_gflops = 0.0;
    int best_mc = 0, best_kc = 0;
    int done = 0;
    for (int mc : mc_values) {
        for (int kc : kc_values) {
            const double best_s = bench::time_min_seconds(opt.trials, [&]() {
                bench::gemm_blocked(A, B, C, n, n, n, mc, kc, nc);
            });
            bench::do_not_optimize(C[0]);
            const double gf = flops / best_s / 1e9;
            if (gf > best_gflops) {
                best_gflops = gf;
                best_mc = mc;
                best_kc = kc;
            }
            bench::JsonWriter pt;
            pt.field("mc", static_cast<long long>(mc));
            pt.field("kc", static_cast<long long>(kc));
            pt.field("nc", static_cast<long long>(nc));
            pt.field("gflops", gf);
            jw.array_object(pt.str());
            bar.update(++done, "Mc=" + std::to_string(mc) + " Kc=" + std::to_string(kc));
        }
    }
    jw.end_array();
    bar.done();

    jw.field("best_mc", static_cast<long long>(best_mc));
    jw.field("best_kc", static_cast<long long>(best_kc));
    jw.field("best_nc", static_cast<long long>(nc));
    jw.field("best_gflops", best_gflops);
    // Arithmetic intensity of GEMM at this size: 2*n^3 FLOPs over the DRAM
    // traffic of three n*n matrices (a lower bound on reuse; used for roofline
    // placement).
    const double bytes = 3.0 * elems * sizeof(float);
    jw.field("arithmetic_intensity", flops / bytes);

    std::free(A);
    std::free(B);
    std::free(C);

    const std::string json = jw.str();
    if (opt.out_path.empty()) {
        std::printf("%s\n", json.c_str());
    } else if (!bench::write_file(opt.out_path, json)) {
        std::fprintf(stderr, "failed to write %s\n", opt.out_path.c_str());
        return 1;
    }
    return 0;
}
