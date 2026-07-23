// stream.cpp
//
// Measures: sustainable memory bandwidth via the four STREAM kernels (Copy,
// Scale, Add, Triad), in three variants (honest scalar, compiler vectorized,
// AVX2 intrinsic with non temporal stores), plus a one to N thread scaling
// curve on the non temporal variant.
//
// Deliberately arranged: arrays are sized to four times the last level cache
// so the working set cannot hide in cache and the number is a true DRAM
// bandwidth. First touch initialization has each worker write its own chunk so
// the pages are resident before timing. Threads pin to P cores first and spill
// to E cores, so the scaling curve shows the P to E knee as a result.
//
// Invalidates the number if broken: a scalar variant that secretly vectorized
// (would erase the honest scalar gap), NT stores on a cache resident array,
// wrong byte accounting (we use the classical STREAM convention), or a missing
// store fence letting NT writes trail past the timer.

#include <barrier>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "bench_utils.hpp"
#include "progress.hpp"
#include "stream_kernels.hpp"
#include "topology.hpp"

namespace {

enum class Variant { Scalar, Vec, Nt };
enum class Kernel { Copy, Scale, Add, Triad };

int kernel_factor(Kernel k) {
    switch (k) {
        case Kernel::Copy: return bench::kCopyFactor;
        case Kernel::Scale: return bench::kScaleFactor;
        case Kernel::Add: return bench::kAddFactor;
        case Kernel::Triad: return bench::kTriadFactor;
    }
    return 0;
}

struct Arrays {
    float* a;
    float* b;
    float* c;
    std::size_t n;
};

// Dispatch one kernel over [begin, end) for a given variant.
void run_chunk(Variant v, Kernel k, const Arrays& ar, std::size_t begin,
               std::size_t end) {
    const std::size_t len = end - begin;
    const float q = 3.0f;
    float* a = ar.a + begin;
    float* b = ar.b + begin;
    float* c = ar.c + begin;
    switch (v) {
        case Variant::Scalar:
            if (k == Kernel::Copy) bench::copy_scalar(c, a, len);
            else if (k == Kernel::Scale) bench::scale_scalar(b, c, q, len);
            else if (k == Kernel::Add) bench::add_scalar(c, a, b, len);
            else bench::triad_scalar(a, b, c, q, len);
            break;
        case Variant::Vec:
            if (k == Kernel::Copy) bench::copy_vec(c, a, len);
            else if (k == Kernel::Scale) bench::scale_vec(b, c, q, len);
            else if (k == Kernel::Add) bench::add_vec(c, a, b, len);
            else bench::triad_vec(a, b, c, q, len);
            break;
        case Variant::Nt:
            if (k == Kernel::Copy) bench::copy_nt(c, a, len);
            else if (k == Kernel::Scale) bench::scale_nt(b, c, q, len);
            else if (k == Kernel::Add) bench::add_nt(c, a, b, len);
            else bench::triad_nt(a, b, c, q, len);
            bench::store_fence();
            break;
    }
}

// Even split of n into T contiguous chunks, each aligned to 8 floats so the
// intrinsic path never straddles a chunk boundary mid vector.
std::pair<std::size_t, std::size_t> chunk_bounds(std::size_t n, int T, int t) {
    const std::size_t per = ((n / T) / 8) * 8;
    const std::size_t begin = static_cast<std::size_t>(t) * per;
    const std::size_t end = (t == T - 1) ? n : begin + per;
    return {begin, end};
}

// Run `kernel` on T threads pinned per cpu_order, `trials` times, return the
// minimum wall time in seconds for the parallel region. Each worker times its
// own chunk between two barriers; worker 0 reduces to the slowest worker per
// trial (that is the region time) and keeps the minimum across trials. Timing
// lives entirely inside the workers, so there is no main thread race and the
// barriers give the happens before needed to read every worker's time safely.
// First touch is done once, before the timed loop.
double time_parallel(Variant v, Kernel k, const Arrays& ar, int T,
                     const std::vector<int>& cpu_order, int trials) {
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
                auto [begin, end] = chunk_bounds(ar.n, T, t);
                // First touch: write this chunk so its pages fault in locally.
                for (std::size_t i = begin; i < end; ++i) {
                    ar.a[i] = 1.0f;
                    ar.b[i] = 2.0f;
                    ar.c[i] = 0.5f;
                }
                for (int r = 0; r < trials; ++r) {
                    sync.arrive_and_wait();  // align start across workers
                    const auto t0 = bench::clock_type::now();
                    run_chunk(v, k, ar, begin, end);  // fence is inside for nt
                    tsec[t] = std::chrono::duration<double>(
                                  bench::clock_type::now() - t0)
                                  .count();
                    sync.arrive_and_wait();  // all times written
                    if (t == 0) {
                        const double slowest =
                            *std::max_element(tsec.begin(), tsec.end());
                        region_min = std::min(region_min, slowest);
                    }
                }
            });
        }
    }  // jthreads join here

    return region_min;
}

double gbs(Kernel k, std::size_t n, double seconds) {
    const double bytes =
        static_cast<double>(n) * kernel_factor(k) * sizeof(float);
    return seconds > 0 ? bytes / seconds / 1e9 : 0.0;
}

std::string kernels_json(Variant v, const Arrays& ar, int T,
                         const std::vector<int>& cpu_order, int trials) {
    bench::JsonWriter jw;
    const Kernel ks[] = {Kernel::Copy, Kernel::Scale, Kernel::Add, Kernel::Triad};
    const char* names[] = {"copy", "scale", "add", "triad"};
    for (int i = 0; i < 4; ++i) {
        const double s = time_parallel(v, ks[i], ar, T, cpu_order, trials);
        jw.field(names[i], gbs(ks[i], ar.n, s));
    }
    return jw.str();
}

struct Options {
    int trials = 5;
    double llc_multiple = 4.0;  // array size = multiple x L3
    int max_threads = -1;       // -1 means all logical CPUs
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
        else if (a == "--trials") o.trials = std::stoi(next());
        else if (a == "--max-threads") o.max_threads = std::stoi(next());
        else if (a == "--mini") {
            o.mini = true;
            o.trials = 3;
            o.llc_multiple = 2.0;
            o.max_threads = 4;
        } else if (a == "--help") {
            std::printf("stream [--out FILE] [--trials N] [--max-threads N] [--mini]\n");
            std::exit(0);
        }
    }
    return o;
}

}  // namespace

int main(int argc, char** argv) {
    const Options opt = parse_args(argc, argv);
    const bench::Topology topo = bench::read_topology();

    // Pin order: P cores first, then E cores, so the scaling knee is visible.
    std::vector<int> cpu_order = topo.p_cores;
    cpu_order.insert(cpu_order.end(), topo.e_cores.begin(), topo.e_cores.end());
    if (cpu_order.empty()) {
        for (int i = 0; i < topo.num_logical; ++i) cpu_order.push_back(i);
    }

    const int max_threads =
        (opt.max_threads > 0) ? opt.max_threads
                              : std::max(1, static_cast<int>(cpu_order.size()));

    // Array size: llc_multiple times L3, split across three arrays. Rounded to
    // a multiple of 8 floats.
    const std::size_t l3 = topo.l3_bytes > 0 ? topo.l3_bytes : 32ull * 1024 * 1024;
    std::size_t n = static_cast<std::size_t>(opt.llc_multiple * l3 / sizeof(float));
    n = (n / 8) * 8;

    Arrays ar;
    ar.n = n;
    ar.a = static_cast<float*>(bench::aligned_alloc_bytes(64, n * sizeof(float)));
    ar.b = static_cast<float*>(bench::aligned_alloc_bytes(64, n * sizeof(float)));
    ar.c = static_cast<float*>(bench::aligned_alloc_bytes(64, n * sizeof(float)));

    // Pin the main thread to the first P core while it drives calibration.
    bench::pin_to_cpu(cpu_order.front());
    const double clock_ghz = bench::measure_clock_ghz_calibrated();

    bench::JsonWriter jw;
    jw.field("benchmark", std::string("stream"));
    jw.field("clock_ghz_measured", clock_ghz);
    jw.field("clock_ghz_cpuinfo", bench::sample_clock_ghz());
    jw.field("element_type", std::string("float32"));
    jw.field("array_bytes", static_cast<long long>(n * sizeof(float)));
    jw.field("array_elements", static_cast<long long>(n));
    jw.field("llc_multiple", opt.llc_multiple);
    jw.field("num_p_cores", static_cast<long long>(topo.p_cores.size()));
    jw.field("num_e_cores", static_cast<long long>(topo.e_cores.size()));
    jw.field("hybrid_known", topo.hybrid_known);

    // Single thread variant comparison (honest scalar vs auto vec vs intrinsic).
    std::fprintf(stderr, "stream: single thread variant comparison\n");
    jw.key("single_thread");
    {
        bench::JsonWriter st;
        st.key("scalar").value_raw(kernels_json(Variant::Scalar, ar, 1, cpu_order, opt.trials));
        st.key("vec").value_raw(kernels_json(Variant::Vec, ar, 1, cpu_order, opt.trials));
        st.key("nt").value_raw(kernels_json(Variant::Nt, ar, 1, cpu_order, opt.trials));
        jw.value_raw(st.str());
    }

    // Thread scaling on the NT variant, one to max_threads.
    bench::Progress bar("stream scaling", max_threads);
    jw.key("scaling");
    bench::JsonWriter sc;
    std::vector<int> thread_counts;
    for (int T = 1; T <= max_threads; ++T) thread_counts.push_back(T);
    sc.begin_array("threads");
    for (int T : thread_counts) sc.array_object(std::to_string(T));
    sc.end_array();

    const Kernel ks[] = {Kernel::Copy, Kernel::Scale, Kernel::Add, Kernel::Triad};
    const char* knames[] = {"copy", "scale", "add", "triad"};
    for (int ki = 0; ki < 4; ++ki) {
        sc.begin_array(knames[ki]);
        for (int T : thread_counts) {
            const double s = time_parallel(Variant::Nt, ks[ki], ar, T, cpu_order, opt.trials);
            sc.array_object(std::to_string(gbs(ks[ki], ar.n, s)));
            if (ki == 3) bar.update(T, std::to_string(T) + " threads");
        }
        sc.end_array();
    }
    jw.value_raw(sc.str());
    bar.done();

    std::free(ar.a);
    std::free(ar.b);
    std::free(ar.c);

    const std::string json = jw.str();
    if (opt.out_path.empty()) {
        std::printf("%s\n", json.c_str());
    } else if (!bench::write_file(opt.out_path, json)) {
        std::fprintf(stderr, "failed to write %s\n", opt.out_path.c_str());
        return 1;
    }
    return 0;
}
