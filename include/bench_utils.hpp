// bench_utils.hpp
//
// What is measured here: nothing directly. This header is the shared
// measurement toolkit every benchmark relies on:
//   - do_not_optimize / clobber_memory: compiler barriers so the optimizer
//     cannot delete or hoist the work being timed (Section 4 rule 4 lives or
//     dies here).
//   - a steady wall clock timer and repeat-and-take-minimum harness.
//   - measured clock sampling from /proc/cpuinfo, because on WSL2 there is no
//     PMU and every cycle denominated number must use the sampled clock, never
//     a spec sheet figure (Section 3).
//   - aligned allocation and a tiny dependency free JSON writer so each
//     benchmark emits a self describing record.
//
// What would invalidate numbers taken with this header: a broken
// do_not_optimize (loop deleted, reads as absurdly fast), or reporting cycles
// against an assumed clock instead of the sampled one.

#ifndef CPU_MICROBENCH_BENCH_UTILS_HPP
#define CPU_MICROBENCH_BENCH_UTILS_HPP

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

namespace bench {

// --------------------------------------------------------------------------
// Anti optimization barriers. These are the single most important lines in
// the whole suite. do_not_optimize forces `value` to be treated as read and
// possibly mutated through memory, so the compiler cannot prove the producing
// computation is dead. clobber_memory is a full compiler memory fence.
//
// Implemented as GCC/Clang inline asm. The "+r,m" constraint lets the value
// live in a register or memory; "memory" clobber blocks reordering.
// --------------------------------------------------------------------------
template <typename T>
inline void do_not_optimize(T& value) {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : "+r,m"(value) : : "memory");
#else
    // Portable fallback: a volatile sink. Weaker, but never silently wrong.
    volatile T sink = value;
    (void)sink;
#endif
}

// Const overload for values we only need the compiler to believe are observed.
template <typename T>
inline void do_not_optimize(const T& value) {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : "r,m"(value) : "memory");
#else
    volatile T sink = value;
    (void)sink;
#endif
}

inline void clobber_memory() {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : : "memory");
#endif
}

// --------------------------------------------------------------------------
// Wall clock timing. steady_clock is monotonic and is the right choice for
// intervals. We never use it for absolute timestamps.
// --------------------------------------------------------------------------
using clock_type = std::chrono::steady_clock;

inline double seconds_since(clock_type::time_point start) {
    const auto end = clock_type::now();
    return std::chrono::duration<double>(end - start).count();
}

// Repeat `fn` `trials` times and return the minimum elapsed seconds. Minimum,
// not mean: the fastest run is the one least perturbed by scheduler noise,
// interrupts, and turbo excursions, which is exactly what we want for a lower
// bound on latency and an upper bound on achievable bandwidth.
template <typename Fn>
inline double time_min_seconds(int trials, Fn&& fn) {
    double best = 1e300;
    for (int i = 0; i < trials; ++i) {
        const auto start = clock_type::now();
        fn();
        const double elapsed = std::chrono::duration<double>(
                                   clock_type::now() - start)
                                   .count();
        best = std::min(best, elapsed);
    }
    return best;
}

// --------------------------------------------------------------------------
// Measured clock. Reads every "cpu MHz" line from /proc/cpuinfo and returns
// the maximum in GHz (the pinned P core under load reports at or near the
// sustained ceiling; the max across siblings is the best single sample of the
// core we care about). Returns 0.0 if the file cannot be read, which callers
// treat as "clock unknown, do not report cycle figures".
// --------------------------------------------------------------------------
inline double sample_clock_ghz() {
    std::ifstream in("/proc/cpuinfo");
    if (!in) return 0.0;
    std::string line;
    double max_mhz = 0.0;
    while (std::getline(in, line)) {
        const auto pos = line.find("cpu MHz");
        if (pos == std::string::npos) continue;
        const auto colon = line.find(':', pos);
        if (colon == std::string::npos) continue;
        try {
            const double mhz = std::stod(line.substr(colon + 1));
            max_mhz = std::max(max_mhz, mhz);
        } catch (...) {
            // Ignore an unparseable line rather than abort a whole run.
        }
    }
    return max_mhz / 1000.0;
}

// Measure the running core clock in software, with no PMU and no reliance on
// /proc/cpuinfo. This is necessary on WSL2, where Hyper-V pins the reported
// "cpu MHz" to a fixed base value (about 3.42 GHz here) that never tracks the
// real boost clock, and where no cpufreq sysfs exists. Underneath, we time a
// dependent chain of integer additions: each `add` has one cycle latency and
// depends on the previous result, so the loop is latency bound at very close
// to one cycle per iteration. Given a known iteration count and the measured
// wall time, frequency = iterations / time. The loop counter decrement and
// branch run on separate ports and do not extend the critical path.
//
// This lands within a couple of percent of the true frequency, which is all a
// cycle label needs, and it correctly follows turbo up to the real boost clock
// the pinned core reaches under load. Returns GHz. Falls back to the (idle,
// possibly wrong) /proc/cpuinfo sample on architectures without an asm path.
inline double measure_clock_ghz_calibrated() {
#if defined(__x86_64__)
    std::uint64_t iters = 2'000'000'000ull;
    const double n0 = static_cast<double>(iters);
    std::uint64_t acc = 0;
    const auto start = clock_type::now();
    asm volatile(
        "1:\n\t"
        "add $1, %[acc]\n\t"   // dependent, one cycle latency
        "dec %[n]\n\t"         // loop counter on a separate port
        "jnz 1b\n\t"
        : [acc] "+r"(acc), [n] "+r"(iters)
        :
        : "cc");
    const double secs = std::chrono::duration<double>(
                            clock_type::now() - start)
                            .count();
    do_not_optimize(acc);
    return secs > 0 ? (n0 / secs) / 1e9 : 0.0;
#elif defined(__aarch64__)
    std::uint64_t iters = 2'000'000'000ull;
    const double n0 = static_cast<double>(iters);
    std::uint64_t acc = 0;
    const auto start = clock_type::now();
    asm volatile(
        "1:\n\t"
        "add %[acc], %[acc], #1\n\t"
        "subs %[n], %[n], #1\n\t"
        "b.ne 1b\n\t"
        : [acc] "+r"(acc), [n] "+r"(iters)
        :
        : "cc");
    const double secs = std::chrono::duration<double>(
                            clock_type::now() - start)
                            .count();
    do_not_optimize(acc);
    return secs > 0 ? (n0 / secs) / 1e9 : 0.0;
#else
    return sample_clock_ghz();
#endif
}

// --------------------------------------------------------------------------
// Aligned allocation. 64 byte alignment matches the cache line so a working
// set boundary is not blurred by a straddling allocation.
// --------------------------------------------------------------------------
inline void* aligned_alloc_bytes(std::size_t alignment, std::size_t size) {
    // std::aligned_alloc requires size to be a multiple of alignment.
    const std::size_t rounded = ((size + alignment - 1) / alignment) * alignment;
    void* p = std::aligned_alloc(alignment, rounded);
    if (!p) {
        std::fprintf(stderr, "aligned_alloc failed for %zu bytes\n", rounded);
        std::abort();
    }
    return p;
}

// --------------------------------------------------------------------------
// Small statistics used across benchmarks.
// --------------------------------------------------------------------------
inline double median(std::vector<double> xs) {
    if (xs.empty()) return 0.0;
    std::sort(xs.begin(), xs.end());
    const std::size_t n = xs.size();
    return (n % 2) ? xs[n / 2] : 0.5 * (xs[n / 2 - 1] + xs[n / 2]);
}

// --------------------------------------------------------------------------
// Minimal JSON writer. No external dependency; the schema is small and fixed.
// Numbers are printed with enough precision to round trip a double. Strings
// are assumed to contain no characters needing escaping (our keys and values
// are compiler flag strings and identifiers), with quotes and backslashes
// handled defensively anyway.
// --------------------------------------------------------------------------
class JsonWriter {
public:
    JsonWriter() { buffer_ << "{"; }

    JsonWriter& key(const std::string& k) {
        comma();
        buffer_ << '"' << escape(k) << "\":";
        need_comma_ = false;
        return *this;
    }

    JsonWriter& value(const std::string& v) {
        buffer_ << '"' << escape(v) << '"';
        need_comma_ = true;
        return *this;
    }

    JsonWriter& value(double v) {
        std::ostringstream os;
        os.precision(17);
        os << v;
        buffer_ << os.str();
        need_comma_ = true;
        return *this;
    }

    JsonWriter& value(long long v) {
        buffer_ << v;
        need_comma_ = true;
        return *this;
    }

    JsonWriter& value(std::size_t v) {
        buffer_ << v;
        need_comma_ = true;
        return *this;
    }

    JsonWriter& value(bool v) {
        buffer_ << (v ? "true" : "false");
        need_comma_ = true;
        return *this;
    }

    // Insert an already formed JSON fragment (object, array, or number token)
    // as the value. Used to nest a composed object under a key.
    JsonWriter& value_raw(const std::string& raw) {
        buffer_ << raw;
        need_comma_ = true;
        return *this;
    }

    // Convenience: key plus value in one call.
    template <typename T>
    JsonWriter& field(const std::string& k, T v) {
        return key(k).value(v);
    }

    JsonWriter& begin_array(const std::string& k) {
        key(k);
        buffer_ << "[";
        need_comma_ = false;
        return *this;
    }

    JsonWriter& end_array() {
        buffer_ << "]";
        need_comma_ = true;
        return *this;
    }

    // Raw object element inside an array (already a complete JSON object).
    JsonWriter& array_object(const std::string& obj) {
        comma();
        buffer_ << obj;
        need_comma_ = true;
        return *this;
    }

    std::string str() {
        return buffer_.str() + "}";
    }

private:
    void comma() {
        if (need_comma_) buffer_ << ",";
    }

    static std::string escape(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            if (c == '"' || c == '\\') out.push_back('\\');
            out.push_back(c);
        }
        return out;
    }

    std::ostringstream buffer_;
    bool need_comma_ = false;
};

// Write text to a path, creating nothing fancy. Returns false on failure.
inline bool write_file(const std::string& path, const std::string& contents) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << contents;
    return static_cast<bool>(out);
}

}  // namespace bench

#endif  // CPU_MICROBENCH_BENCH_UTILS_HPP
