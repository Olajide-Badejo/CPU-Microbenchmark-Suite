// perf_event.hpp
//
// A thin wrapper over the Linux perf_event_open syscall for hardware counters
// (cycles, instructions, cache misses). It is compiled everywhere but is only
// functional on bare metal Linux: WSL2 exposes no PMU, so is_available()
// returns false there and the whole suite falls back to the timing plus known
// transfer size methodology (Section 3). Nothing in the core measurement path
// depends on this; it exists so a bare metal run can optionally cross check
// the timed numbers against real cycle counts.
//
// What would invalidate a number: reporting a PMU derived figure from a host
// where is_available() is false. Callers must check it first.

#ifndef CPU_MICROBENCH_PERF_EVENT_HPP
#define CPU_MICROBENCH_PERF_EVENT_HPP

#include <cstdint>

#if defined(__linux__)
#include <asm/unistd.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cstring>
#endif

namespace bench {

class PerfCounter {
public:
    // Opens a hardware cycle counter for the calling process on any CPU.
    PerfCounter() {
#if defined(__linux__)
        struct perf_event_attr attr;
        std::memset(&attr, 0, sizeof(attr));
        attr.type = PERF_TYPE_HARDWARE;
        attr.size = sizeof(attr);
        attr.config = PERF_COUNT_HW_CPU_CYCLES;
        attr.disabled = 1;
        attr.exclude_kernel = 1;
        attr.exclude_hv = 1;
        fd_ = static_cast<int>(
            syscall(__NR_perf_event_open, &attr, 0, -1, -1, 0));
#endif
    }

    ~PerfCounter() {
#if defined(__linux__)
        if (fd_ != -1) close(fd_);
#endif
    }

    PerfCounter(const PerfCounter&) = delete;
    PerfCounter& operator=(const PerfCounter&) = delete;

    bool is_available() const { return fd_ != -1; }

    void start() {
#if defined(__linux__)
        if (fd_ == -1) return;
        ioctl(fd_, PERF_EVENT_IOC_RESET, 0);
        ioctl(fd_, PERF_EVENT_IOC_ENABLE, 0);
#endif
    }

    std::uint64_t stop() {
#if defined(__linux__)
        if (fd_ == -1) return 0;
        ioctl(fd_, PERF_EVENT_IOC_DISABLE, 0);
        std::uint64_t count = 0;
        if (read(fd_, &count, sizeof(count)) != sizeof(count)) return 0;
        return count;
#else
        return 0;
#endif
    }

private:
    int fd_ = -1;
};

}  // namespace bench

#endif  // CPU_MICROBENCH_PERF_EVENT_HPP
