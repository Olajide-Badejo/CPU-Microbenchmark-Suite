// test_util.hpp
//
// A dependency free micro test harness. Each test is a plain main() that calls
// CHECK / CHECK_EQ / CHECK_NEAR and ends with return test::summary(). CTest
// keys off the process exit code, so no framework is needed. Kept tiny on
// purpose: the interesting logic belongs in the code under test, not here.

#ifndef CPU_MICROBENCH_TEST_UTIL_HPP
#define CPU_MICROBENCH_TEST_UTIL_HPP

#include <cmath>
#include <cstdio>
#include <string>

namespace test {

inline int& failures() {
    static int f = 0;
    return f;
}

inline void report(bool ok, const char* expr, const char* file, int line) {
    if (!ok) {
        ++failures();
        std::fprintf(stderr, "FAIL %s:%d  %s\n", file, line, expr);
    }
}

inline int summary() {
    if (failures() == 0) {
        std::printf("all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d check(s) failed\n", failures());
    return 1;
}

}  // namespace test

#define CHECK(cond) ::test::report((cond), #cond, __FILE__, __LINE__)

#define CHECK_EQ(a, b)                                                   \
    ::test::report((a) == (b), #a " == " #b, __FILE__, __LINE__)

#define CHECK_NEAR(a, b, tol)                                            \
    ::test::report(std::fabs((a) - (b)) <= (tol), #a " ~= " #b, __FILE__, \
                   __LINE__)

#endif  // CPU_MICROBENCH_TEST_UTIL_HPP
