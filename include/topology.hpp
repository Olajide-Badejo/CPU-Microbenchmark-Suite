// topology.hpp
//
// What is measured here: nothing timed. This reads the machine's cache
// capacities from sysfs (the ground truth the plateau detector is cross
// checked against) and identifies which logical CPUs are performance (P) cores
// versus efficiency (E) cores, then pins the calling thread to a chosen CPU.
//
// What is deliberately handled: the WSL2 case. WSL2 exposes the cache index
// files but not the /sys/devices/cpu_core and cpu_atom hybrid split, and it
// reports a uniform L2 size across P and E cores, so P/E cannot be recovered
// from sysfs there. When the split is absent we fall back to the Intel CPU
// enumeration convention (P core SMT threads first, E cores last) and record
// that the identification is heuristic, not measured. The parser takes a root
// path so the unit test can drive it against committed fixtures, including a
// synthetic hybrid machine that exercises the split detection WSL cannot.
//
// What would invalidate results built on this: silently trusting a heuristic
// P/E label as if it were measured. Callers must surface topo.hybrid_known.

#ifndef CPU_MICROBENCH_TOPOLOGY_HPP
#define CPU_MICROBENCH_TOPOLOGY_HPP

#include <cctype>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#if defined(__linux__)
#include <sched.h>
#endif

namespace bench {

struct Topology {
    std::size_t l1d_bytes = 0;
    std::size_t l1i_bytes = 0;
    std::size_t l2_bytes = 0;
    std::size_t l3_bytes = 0;
    int num_logical = 0;
    std::vector<int> p_cores;  // logical CPU ids identified as P cores
    std::vector<int> e_cores;  // logical CPU ids identified as E cores
    bool hybrid_known = false;  // true only when sysfs gave us the real split
};

// Parse a sysfs cache size string such as "48K", "2048K", "33792K", "1M".
inline std::size_t parse_cache_size(const std::string& raw) {
    std::string s;
    for (char c : raw) {
        if (!std::isspace(static_cast<unsigned char>(c))) s.push_back(c);
    }
    if (s.empty()) return 0;
    std::size_t mult = 1;
    char suffix = s.back();
    if (suffix == 'K' || suffix == 'k') {
        mult = 1024;
        s.pop_back();
    } else if (suffix == 'M' || suffix == 'm') {
        mult = 1024 * 1024;
        s.pop_back();
    } else if (suffix == 'G' || suffix == 'g') {
        mult = 1024ull * 1024 * 1024;
        s.pop_back();
    }
    try {
        return static_cast<std::size_t>(std::stoull(s)) * mult;
    } catch (...) {
        return 0;
    }
}

namespace detail {

inline std::string read_first_line(const std::string& path) {
    std::ifstream in(path);
    if (!in) return "";
    std::string line;
    std::getline(in, line);
    return line;
}

// Parse a Linux cpu list string such as "0-15" or "0,2,4-7" into ids.
inline std::vector<int> parse_cpu_list(const std::string& raw) {
    std::vector<int> out;
    std::stringstream ss(raw);
    std::string token;
    while (std::getline(ss, token, ',')) {
        const auto dash = token.find('-');
        if (dash == std::string::npos) {
            try {
                out.push_back(std::stoi(token));
            } catch (...) {
            }
        } else {
            try {
                const int lo = std::stoi(token.substr(0, dash));
                const int hi = std::stoi(token.substr(dash + 1));
                for (int i = lo; i <= hi; ++i) out.push_back(i);
            } catch (...) {
            }
        }
    }
    return out;
}

}  // namespace detail

// Read cache capacities and hybrid split from a sysfs root (default "/sys").
inline Topology read_topology(const std::string& sysfs_root = "/sys") {
    Topology topo;
    const std::string cpu0 = sysfs_root + "/devices/system/cpu/cpu0/cache";

    for (int idx = 0; idx < 16; ++idx) {
        const std::string base = cpu0 + "/index" + std::to_string(idx);
        const std::string level_s = detail::read_first_line(base + "/level");
        if (level_s.empty()) break;  // no more indices
        const std::string type = detail::read_first_line(base + "/type");
        const std::size_t size =
            parse_cache_size(detail::read_first_line(base + "/size"));
        int level = 0;
        try {
            level = std::stoi(level_s);
        } catch (...) {
            continue;
        }
        if (level == 1 && type == "Data") topo.l1d_bytes = size;
        else if (level == 1 && type == "Instruction") topo.l1i_bytes = size;
        else if (level == 2) topo.l2_bytes = size;
        else if (level == 3) topo.l3_bytes = size;
    }

    // Count logical CPUs by walking cpuN directories.
    int n = 0;
    while (true) {
        const std::string p =
            sysfs_root + "/devices/system/cpu/cpu" + std::to_string(n) + "/cache";
        std::ifstream probe(p + "/index0/level");
        const std::string online_probe =
            sysfs_root + "/devices/system/cpu/cpu" + std::to_string(n);
        std::ifstream dir_probe(online_probe + "/cache/index0/size");
        if (!dir_probe && !probe) break;
        ++n;
        if (n > 4096) break;  // safety
    }
    topo.num_logical = n;

    // Hybrid split: present on bare metal Raptor Lake, absent on WSL2.
    const std::string p_cpus =
        detail::read_first_line(sysfs_root + "/devices/cpu_core/cpus");
    const std::string e_cpus =
        detail::read_first_line(sysfs_root + "/devices/cpu_atom/cpus");
    if (!p_cpus.empty() || !e_cpus.empty()) {
        topo.p_cores = detail::parse_cpu_list(p_cpus);
        topo.e_cores = detail::parse_cpu_list(e_cpus);
        topo.hybrid_known = true;
    } else {
        topo.hybrid_known = false;
        // Heuristic fallback for the i7-14700K under WSL: 8 P cores with SMT
        // (16 threads, ids 0..15) then 12 E cores (ids 16..27). We encode the
        // convention "low ids are P" without asserting it is measured.
        if (topo.num_logical > 0) {
            const int guess_p = topo.num_logical >= 20
                                    ? 16   // 14700K layout
                                    : topo.num_logical / 2;
            for (int i = 0; i < topo.num_logical; ++i) {
                if (i < guess_p) topo.p_cores.push_back(i);
                else topo.e_cores.push_back(i);
            }
        }
    }
    return topo;
}

// A sensible default P core to pin to (Section 3: P core by default).
inline int default_p_core(const Topology& topo) {
    return topo.p_cores.empty() ? 0 : topo.p_cores.front();
}

// A default E core for the reported P versus E comparison.
inline int default_e_core(const Topology& topo) {
    if (!topo.e_cores.empty()) return topo.e_cores.front();
    return topo.num_logical > 0 ? topo.num_logical - 1 : 0;
}

// Pin the calling thread to a single logical CPU. Returns true on success.
inline bool pin_to_cpu(int cpu) {
#if defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    return sched_setaffinity(0, sizeof(set), &set) == 0;
#else
    (void)cpu;
    return false;
#endif
}

}  // namespace bench

#endif  // CPU_MICROBENCH_TOPOLOGY_HPP
