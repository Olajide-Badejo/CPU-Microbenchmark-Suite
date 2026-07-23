// peak_flops.cpp
//
// Measures: peak single precision FMA throughput in GFLOPS, on one pinned P
// core and across all cores, checked against the ceiling computed from the
// measured (not spec sheet) clock. The kernel is the ten chain FMA loop in
// flops_kernel.hpp.
//
// The single core ceiling is clock * lanes * 2 flops per FMA * fma ports. On
// this Raptor Cove core: lanes 8, two FMA ports, so 32 FLOPs per cycle. The
// reported efficiency is measured over this ceiling; anything near or above the
// ceiling would mean the clock or the counting is wrong, which is why the
// comparison is in the output.

#include <algorithm>
#include <barrier>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "bench_utils.hpp"
#include "flops_kernel.hpp"
#include "topology.hpp"

namespace {

struct Options {
    std::uint64_t iters = 40'000'000;  // per trial
    int trials = 5;
    int max_threads = -1;
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
        else if (a == "--iters") o.iters = std::stoull(next());
        else if (a == "--trials") o.trials = std::stoi(next());
        else if (a == "--max-threads") o.max_threads = std::stoi(next());
        else if (a == "--mini") {
            o.mini = true;
            o.iters = 8'000'000;
            o.trials = 3;
            o.max_threads = 4;
        } else if (a == "--help") {
            std::printf("peak_flops [--out FILE] [--iters N] [--trials N] "
                        "[--max-threads N] [--mini]\n");
            std::exit(0);
        }
    }
    return o;
}

// GFLOPS for one thread running `iters` per trial, `trials` times, min time.
double single_gflops(std::uint64_t iters, int trials) {
    volatile float sink = 0.0f;
    const double best = bench::time_min_seconds(trials, [&]() {
        sink = bench::flops_kernel_run(iters);
    });
    (void)sink;
    return (bench::kFlopsPerIter * static_cast<double>(iters)) / best / 1e9;
}

// Aggregate GFLOPS across T threads pinned per cpu_order. Each worker times its
// own run; worker 0 reduces the slowest per trial and keeps the minimum. Total
// FLOPs is T * per thread FLOPs, divided by the slowest thread's time.
double aggregate_gflops(std::uint64_t iters, int trials, int T,
                        const std::vector<int>& cpu_order) {
    std::barrier sync(T);
    std::vector<double> tsec(static_cast<std::size_t>(T), 0.0);
    double region_min = 1e300;
    {
        std::vector<std::jthread> workers;
        workers.reserve(T);
        for (int t = 0; t < T; ++t) {
            workers.emplace_back([&, t]() {
                if (t < static_cast<int>(cpu_order.size())) {
                    bench::pin_to_cpu(cpu_order[t]);
                }
                volatile float sink = 0.0f;
                for (int r = 0; r < trials; ++r) {
                    sync.arrive_and_wait();
                    const auto t0 = bench::clock_type::now();
                    sink = bench::flops_kernel_run(iters);
                    tsec[t] = std::chrono::duration<double>(
                                  bench::clock_type::now() - t0)
                                  .count();
                    sync.arrive_and_wait();
                    if (t == 0) {
                        region_min = std::min(
                            region_min,
                            *std::max_element(tsec.begin(), tsec.end()));
                    }
                }
                (void)sink;
            });
        }
    }
    const double total_flops =
        bench::kFlopsPerIter * static_cast<double>(iters) * T;
    return total_flops / region_min / 1e9;
}

}  // namespace

int main(int argc, char** argv) {
    const Options opt = parse_args(argc, argv);
    const bench::Topology topo = bench::read_topology();

    std::vector<int> cpu_order = topo.p_cores;
    cpu_order.insert(cpu_order.end(), topo.e_cores.begin(), topo.e_cores.end());
    if (cpu_order.empty()) {
        for (int i = 0; i < topo.num_logical; ++i) cpu_order.push_back(i);
    }
    const int all_threads =
        (opt.max_threads > 0) ? opt.max_threads
                              : std::max(1, static_cast<int>(cpu_order.size()));

    const int p_core = bench::default_p_core(topo);
    bench::pin_to_cpu(p_core);
    const double clock_ghz = bench::measure_clock_ghz_calibrated();

    // Single core ceiling: clock * lanes * 2 (FMA) * 2 FMA ports.
    const int fma_ports = 2;  // Raptor Cove and Gracemont both, ports 0 and 1
    const double ceiling_single =
        clock_ghz * bench::kFlopsLanes * 2.0 * fma_ports;

    std::fprintf(stderr, "peak_flops: single P core (cpu %d)\n", p_core);
    const double gf_single = single_gflops(opt.iters, opt.trials);

    std::fprintf(stderr, "peak_flops: all core (%d threads)\n", all_threads);
    const double gf_all =
        aggregate_gflops(opt.iters, opt.trials, all_threads, cpu_order);

    bench::JsonWriter jw;
    jw.field("benchmark", std::string("peak_flops"));
    jw.field("clock_ghz_measured", clock_ghz);
    jw.field("clock_ghz_cpuinfo", bench::sample_clock_ghz());
    jw.field("chains", static_cast<long long>(bench::kFlopsChains));
    jw.field("simd_lanes", static_cast<long long>(bench::kFlopsLanes));
    jw.field("fma_ports_assumed", static_cast<long long>(fma_ports));
    jw.field("ceiling_single_gflops", ceiling_single);
    jw.field("single_core_cpu", static_cast<long long>(p_core));
    jw.field("single_core_gflops", gf_single);
    jw.field("single_core_efficiency", ceiling_single > 0 ? gf_single / ceiling_single : 0.0);
    jw.field("all_core_threads", static_cast<long long>(all_threads));
    jw.field("all_core_gflops", gf_all);
    jw.field("hybrid_known", topo.hybrid_known);

    const std::string json = jw.str();
    if (opt.out_path.empty()) {
        std::printf("%s\n", json.c_str());
    } else if (!bench::write_file(opt.out_path, json)) {
        std::fprintf(stderr, "failed to write %s\n", opt.out_path.c_str());
        return 1;
    }
    return 0;
}
