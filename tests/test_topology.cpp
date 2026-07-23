// test_topology.cpp
//
// Exercises the sysfs parser against two committed fixtures:
//   - sysfs_flat: a WSL2 style tree with cache index files but no hybrid split.
//   - sysfs_hybrid: a bare metal style tree that also exposes cpu_core and
//     cpu_atom, which WSL2 cannot provide, so the P/E detection path is tested
//     here rather than on the live machine.
// It also unit tests the two string parsers directly.

#include <string>

#include "test_util.hpp"
#include "topology.hpp"

#ifndef FIXTURE_DIR
#define FIXTURE_DIR "."
#endif

int main() {
    const std::string flat = std::string(FIXTURE_DIR) + "/sysfs_flat";
    const std::string hybrid = std::string(FIXTURE_DIR) + "/sysfs_hybrid";

    // Size string parser.
    CHECK_EQ(bench::parse_cache_size("48K"), std::size_t{48 * 1024});
    CHECK_EQ(bench::parse_cache_size("2048K"), std::size_t{2048 * 1024});
    CHECK_EQ(bench::parse_cache_size("33792K"), std::size_t{33792ull * 1024});
    CHECK_EQ(bench::parse_cache_size("1M"), std::size_t{1024 * 1024});
    CHECK_EQ(bench::parse_cache_size(""), std::size_t{0});

    // Cpu list parser.
    {
        auto v = bench::detail::parse_cpu_list("0-3");
        CHECK_EQ(v.size(), std::size_t{4});
        CHECK_EQ(v.front(), 0);
        CHECK_EQ(v.back(), 3);
        auto w = bench::detail::parse_cpu_list("0,2,4-6");
        CHECK_EQ(w.size(), std::size_t{5});
        CHECK_EQ(w.back(), 6);
    }

    // Flat fixture: caches parsed, no measured hybrid split.
    {
        const auto topo = bench::read_topology(flat);
        CHECK_EQ(topo.l1d_bytes, std::size_t{48 * 1024});
        CHECK_EQ(topo.l1i_bytes, std::size_t{32 * 1024});
        CHECK_EQ(topo.l2_bytes, std::size_t{2048 * 1024});
        CHECK_EQ(topo.l3_bytes, std::size_t{33792ull * 1024});
        CHECK(!topo.hybrid_known);
    }

    // Hybrid fixture: same caches, plus a real P/E split from sysfs.
    {
        const auto topo = bench::read_topology(hybrid);
        CHECK_EQ(topo.l1d_bytes, std::size_t{48 * 1024});
        CHECK_EQ(topo.l3_bytes, std::size_t{33792ull * 1024});
        CHECK(topo.hybrid_known);
        CHECK_EQ(topo.p_cores.size(), std::size_t{16});  // 0..15
        CHECK_EQ(topo.e_cores.size(), std::size_t{12});  // 16..27
        CHECK_EQ(topo.p_cores.front(), 0);
        CHECK_EQ(topo.e_cores.front(), 16);
        CHECK_EQ(bench::default_p_core(topo), 0);
        CHECK_EQ(bench::default_e_core(topo), 16);
    }

    return test::summary();
}
