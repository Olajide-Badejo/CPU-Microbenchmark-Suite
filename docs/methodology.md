# Methodology

The full measurement protocol. The short version: every number is timing plus a
known transfer size or operation count, and every platform caveat is stated.

## No hardware counters

WSL2 runs under a Hyper-V utility VM that exposes no performance monitoring
unit. The suite therefore derives everything from wall clock timing, the
classical STREAM approach (McCalpin 1995). A `perf_event` wrapper is compiled
but gated to bare metal Linux, where `is_available()` returns true; under WSL2
it stays dormant.

## Clock measurement

Cycle denominated figures need the frequency the core actually ran at. Under
WSL2:

- `/proc/cpuinfo` reports `cpu MHz` pinned to a base value near 3.42 GHz that
  never moves under load.
- There is no `cpufreq` sysfs interface.
- The pinned core actually boosts to about 5.3 GHz.

Using the base value would understate every cycle count by about 1.6x. The suite
measures the running clock in software: a dependent chain of integer additions
runs at one cycle per iteration (each add depends on the previous result; the
loop counter runs on a separate port), so frequency is iterations over elapsed
time. Because the chain cannot run faster than one cycle per iteration, any
perturbation only lengthens the time, so the suite warms up once and takes the
maximum frequency over four passes. This reports about 5.3 GHz under load, at
which L1 latency lands at exactly 5 cycles. The wrong cpuinfo value is recorded
as `clock_ghz_cpuinfo` for transparency.

## Turbo stability

For a stable clock, set the Windows power plan maximum processor state to 99
percent before a session:

```powershell
powercfg /setacvalueindex SCHEME_CURRENT SUB_PROCESSOR PROCTHROTTLEMAX 99
powercfg /setactive SCHEME_CURRENT
```

This caps turbo so the boost clock does not wander. The software clock is
sampled every run regardless, so a session without the cap is still honest, only
noisier.

## Pinning and topology

Benchmarks pin to a single logical CPU with `sched_setaffinity`, a P core by
default. WSL2 does not expose the `cpu_core` and `cpu_atom` hybrid split and
reports a uniform L2 across cores, so P and E cannot be recovered from sysfs. The
topology reader detects the split when present and otherwise falls back to the
Intel enumeration (P core SMT threads first, E cores last), marking the result
`hybrid_known = false`.

## Working set sizing

- Latency: powers of two from 4 KB to 512 MB, so every cache level and DRAM is
  covered.
- Bandwidth: arrays sized to four times the L3 so the data cannot hide in cache.

## What a timing derived number can claim

It can claim a latency, a bandwidth, or a throughput, each traceable to a run.
It cannot claim a direct hardware cycle or cache miss count. The sanity gates
(L1 3 to 7 cycles, DRAM 50 to 120 ns, single thread bandwidth 30 to 65 GB/s,
single P core 100 to 200 GFLOPS) flag implausible results for investigation
rather than publishing them.

## Reproducibility

Every run records compiler and flags, kernel version, WSL flag, measured clock,
pinned core, and git commit into `summary.json`, which is the single input to
all plots and tables.
