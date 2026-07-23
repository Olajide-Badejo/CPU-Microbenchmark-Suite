// progress.hpp
//
// A TTY aware progress helper shared by the benchmarks. On an interactive
// terminal it redraws a single line with a bar, percent, the current label,
// and a rolling ETA. When stdout is not a terminal, or when CI is set, it
// prints plain one line updates instead so CI logs stay readable and do not
// fill with carriage returns.

#ifndef CPU_MICROBENCH_PROGRESS_HPP
#define CPU_MICROBENCH_PROGRESS_HPP

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

namespace bench {

class Progress {
public:
    Progress(std::string title, int total_steps)
        : title_(std::move(title)),
          total_(total_steps > 0 ? total_steps : 1),
          start_(std::chrono::steady_clock::now()) {
        interactive_ = detect_tty();
    }

    void update(int step, const std::string& label) {
        step_ = step;
        const double frac = static_cast<double>(step) / total_;
        const double elapsed = std::chrono::duration<double>(
                                   std::chrono::steady_clock::now() - start_)
                                   .count();
        const double eta = frac > 0.0 ? elapsed * (1.0 - frac) / frac : 0.0;

        if (interactive_) {
            const int width = 30;
            const int filled = static_cast<int>(frac * width);
            std::string bar(filled, '#');
            bar.resize(width, '.');
            std::fprintf(stderr, "\r%s [%s] %3.0f%%  %-28s ETA %4.0fs",
                         title_.c_str(), bar.c_str(), frac * 100.0,
                         label.c_str(), eta);
            std::fflush(stderr);
        } else {
            std::fprintf(stderr, "%s %3.0f%% (%d/%d) %s  ETA %.0fs\n",
                         title_.c_str(), frac * 100.0, step, total_,
                         label.c_str(), eta);
        }
    }

    void done() {
        if (interactive_) std::fprintf(stderr, "\n");
        const double elapsed = std::chrono::duration<double>(
                                   std::chrono::steady_clock::now() - start_)
                                   .count();
        std::fprintf(stderr, "%s complete in %.1fs\n", title_.c_str(), elapsed);
    }

private:
    static bool detect_tty() {
        if (std::getenv("CI") != nullptr) return false;
#if defined(__unix__) || defined(__APPLE__)
        return isatty(fileno(stderr)) != 0;
#else
        return false;
#endif
    }

    std::string title_;
    int total_;
    int step_ = 0;
    bool interactive_ = false;
    std::chrono::steady_clock::time_point start_;
};

}  // namespace bench

#endif  // CPU_MICROBENCH_PROGRESS_HPP
