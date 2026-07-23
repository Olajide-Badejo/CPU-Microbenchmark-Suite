// pointer_chase.cpp
//
// Measures: dependent load use latency across working sets from 4 KB to 512 MB
// in powers of two, in nanoseconds per load and in cycles at the measured
// clock. Each cache plateau (L1, L2, L3, DRAM) shows up as a step in the
// curve.
//
// Deliberately defeated: the stride prefetcher, by chasing a random single
// cycle permutation at 64 byte stride (see pointer_chase.hpp). Out of order
// execution cannot hide the latency because each load address depends on the
// previous load's result.
//
// Invalidates the number if broken: a multi cycle permutation (guarded by an
// internal is_single_cycle assertion and by test_permutation.cpp), or the
// compiler deleting the chase loop (guarded by do_not_optimize on the walking
// pointer).

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "bench_utils.hpp"
#include "pointer_chase.hpp"
#include "progress.hpp"
#include "topology.hpp"

namespace {

struct Options {
    std::size_t min_bytes = 4ull * 1024;          // 4 KB
    std::size_t max_bytes = 512ull * 1024 * 1024;  // 512 MB
    int cpu = -1;                                   // -1 means pick a P core
    int trials = 5;
    double target_seconds = 0.4;  // per trial timed window
    std::string out_path;         // empty means stdout
    std::uint64_t seed = 0x9E3779B97F4A7C15ull;
    bool mini = false;  // fast smoke mode for CI and integration test
};

Options parse_args(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            return (i + 1 < argc) ? argv[++i] : std::string();
        };
        if (a == "--out") o.out_path = next();
        else if (a == "--cpu") o.cpu = std::stoi(next());
        else if (a == "--trials") o.trials = std::stoi(next());
        else if (a == "--max-mb") o.max_bytes = std::stoull(next()) * 1024 * 1024;
        else if (a == "--min-kb") o.min_bytes = std::stoull(next()) * 1024;
        else if (a == "--seed") o.seed = std::stoull(next());
        else if (a == "--mini") {
            o.mini = true;
            o.max_bytes = 64ull * 1024 * 1024;  // enough to reach DRAM quickly
            o.trials = 3;
            o.target_seconds = 0.15;
        } else if (a == "--help") {
            std::printf(
                "pointer_chase [--out FILE] [--cpu N] [--trials N] "
                "[--max-mb N] [--min-kb N] [--mini]\n");
            std::exit(0);
        }
    }
    return o;
}

// One measured point. Returns latency in ns per dependent load.
double measure_latency_ns(std::size_t bytes, const Options& opt) {
    const std::size_t n = bytes / bench::kChaseStride;
    if (n < 2) return 0.0;

    // Backing store: n lines of 64 bytes, first 8 bytes of each holds the
    // pointer to the next line. 64 byte aligned so a line never straddles.
    auto* buf = static_cast<unsigned char*>(
        bench::aligned_alloc_bytes(64, n * bench::kChaseStride));

    const std::vector<std::size_t> next = bench::build_cycle_next(n, opt.seed);
    // Internal safety net: a split permutation would invalidate this point.
    if (!bench::is_single_cycle(next)) {
        std::fprintf(stderr, "FATAL: permutation is not a single cycle at %zu bytes\n",
                     bytes);
        std::free(buf);
        std::exit(2);
    }

    for (std::size_t i = 0; i < n; ++i) {
        auto** slot = reinterpret_cast<unsigned char**>(buf + i * bench::kChaseStride);
        *slot = buf + next[i] * bench::kChaseStride;
    }

    // Warm up: one full traversal to fault pages in and settle the caches for
    // sizes that fit. Also lets us calibrate steps for the timed window.
    unsigned char* p = buf;
    for (std::size_t i = 0; i < n; ++i) {
        p = *reinterpret_cast<unsigned char**>(p);
    }
    bench::do_not_optimize(p);

    // Calibrate: time a small burst, then size the real window to ~target time.
    const std::size_t calib_steps = 1u << 16;
    const double calib_s = bench::time_min_seconds(1, [&]() {
        unsigned char* q = buf;
        for (std::size_t i = 0; i < calib_steps; ++i) {
            q = *reinterpret_cast<unsigned char**>(q);
        }
        bench::do_not_optimize(q);
    });
    const double ns_per_step_est =
        calib_s > 0 ? calib_s / calib_steps * 1e9 : 1.0;
    std::size_t steps = static_cast<std::size_t>(
        opt.target_seconds / (ns_per_step_est * 1e-9));
    if (steps < calib_steps) steps = calib_steps;

    const double best_s = bench::time_min_seconds(opt.trials, [&]() {
        unsigned char* q = buf;
        for (std::size_t i = 0; i < steps; ++i) {
            q = *reinterpret_cast<unsigned char**>(q);
        }
        bench::do_not_optimize(q);
    });

    std::free(buf);
    return best_s / static_cast<double>(steps) * 1e9;
}

}  // namespace

int main(int argc, char** argv) {
    const Options opt = parse_args(argc, argv);

    const bench::Topology topo = bench::read_topology();
    const int cpu = (opt.cpu >= 0) ? opt.cpu : bench::default_p_core(topo);
    const bool pinned = bench::pin_to_cpu(cpu);

    // Measure the running clock in software (WSL2 /proc/cpuinfo is pinned to a
    // wrong base value), and record the cpuinfo reading too for transparency.
    const double clock_ghz = bench::measure_clock_ghz_calibrated();
    const double clock_ghz_cpuinfo = bench::sample_clock_ghz();

    // Build the list of working set sizes (powers of two).
    std::vector<std::size_t> sizes;
    for (std::size_t b = opt.min_bytes; b <= opt.max_bytes; b *= 2) {
        sizes.push_back(b);
    }

    bench::Progress bar("pointer_chase", static_cast<int>(sizes.size()));
    bench::JsonWriter jw;
    jw.field("benchmark", std::string("pointer_chase"));
    jw.field("cpu_pinned", static_cast<long long>(cpu));
    jw.field("cpu_pin_ok", pinned);
    jw.field("clock_ghz_measured", clock_ghz);
    jw.field("clock_ghz_cpuinfo", clock_ghz_cpuinfo);
    jw.field("stride_bytes", static_cast<long long>(bench::kChaseStride));
    jw.field("sysfs_l1d_bytes", static_cast<long long>(topo.l1d_bytes));
    jw.field("sysfs_l2_bytes", static_cast<long long>(topo.l2_bytes));
    jw.field("sysfs_l3_bytes", static_cast<long long>(topo.l3_bytes));
    jw.field("hybrid_known", topo.hybrid_known);
    jw.begin_array("points");

    for (std::size_t i = 0; i < sizes.size(); ++i) {
        const std::size_t bytes = sizes[i];
        char label[64];
        std::snprintf(label, sizeof(label), "%.0f KB",
                      static_cast<double>(bytes) / 1024.0);
        bar.update(static_cast<int>(i), label);

        const double lat_ns = measure_latency_ns(bytes, opt);
        const double cycles = clock_ghz > 0 ? lat_ns * clock_ghz : 0.0;

        bench::JsonWriter pt;
        pt.field("bytes", static_cast<long long>(bytes));
        pt.field("kib", static_cast<double>(bytes) / 1024.0);
        pt.field("latency_ns", lat_ns);
        pt.field("cycles", cycles);
        jw.array_object(pt.str());
    }
    jw.end_array();
    bar.done();

    const std::string json = jw.str();
    if (opt.out_path.empty()) {
        std::printf("%s\n", json.c_str());
    } else if (!bench::write_file(opt.out_path, json)) {
        std::fprintf(stderr, "failed to write %s\n", opt.out_path.c_str());
        return 1;
    }
    return 0;
}
