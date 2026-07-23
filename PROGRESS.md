# PROGRESS

Living build log. One entry per meaningful unit of work, newest phase at the
bottom. Never mark a phase complete without running its checks and pasting the
output into the commit that closes it.

## Target machine (measured at Phase 0)

- CPU: Intel Core i7-14700K (8 P cores Raptor Cove, 12 E cores Gracemont), 28
  logical CPUs visible to WSL2, AVX2 and FMA present, no AVX-512 (confirmed
  absent in `/proc/cpuinfo`).
- L1D reported by sysfs on cpu0: 48 KB (matches Raptor Cove).
- OS: Windows 11 Pro, suite runs inside WSL2 Ubuntu 26.04 LTS.
- RAM: 32 GB DDR5-5600 dual channel (89.6 GB/s theoretical).

## Toolchain (verified at Phase 0)

| Tool | Spec floor | Found | Note |
|---|---|---|---|
| g++ | 16.1 | 15.2.0 | SUBSTITUTION. GCC 16 is not released yet. 15.2 has full C++23 support, which is all this suite needs. |
| cmake | 4.4 | 4.2.3 | SUBSTITUTION. 4.2 satisfies every feature used here (`cmake_minimum_required(3.28)`). |
| python3 | 3.x | 3.14.4 | numpy 2.3.5, scipy 1.16.3, matplotlib 3.10.7, tqdm 4.67.3 all present. |
| ruff | any | 0.15.22 | |
| latexmk | any | 4.87 | TeX Live present in WSL. |
| aarch64-linux-gnu-g++ | required | not installed | Installed on demand in Phase 4 via apt (sudo nopasswd confirmed working). |
| qemu-aarch64 | required | not installed | Same, Phase 4. |

## Measurement honesty (restated so it is never forgotten)

WSL2 exposes no hardware PMU. Every number is timing plus known transfer size
(McCalpin STREAM approach). The `perf_event` path is compiled but gated to bare
metal Linux. Turbo is pinned by setting the Windows power plan maximum
processor state to 99 percent; the sustained clock is sampled from
`/proc/cpuinfo` during every run and every cycle denominated figure uses that
measured clock, never a spec sheet number.

### Power plan step (manual, done once per machine)

Run in an elevated Windows PowerShell before a measurement session:

```powershell
powercfg /setacvalueindex SCHEME_CURRENT SUB_PROCESSOR PROCTHROTTLEMAX 99
powercfg /setactive SCHEME_CURRENT
```

This caps turbo so the sampled clock is stable. `run_suite.sh` records the
sampled clock regardless, so a session run without this step is still honest,
just noisier; the recorded clock trace is the ground truth.

## Phase log

### Phase 0: toolchain + power plan + dash lint  (IN PROGRESS)

- [x] Verified WSL2 Ubuntu 26.04, toolchain versions (table above).
- [x] `git init` on `main`, author Olajide Badejo, `core.autocrlf false`.
- [x] `.gitattributes` forces LF so the dash check is identical on every OS.
- [x] MIT LICENSE, sole author Olajide Badejo.
- [x] Attribution disabled via `.claude/settings.local.json`.
- [x] `scripts/check_no_dashes.py` written and passing.
- [x] CMake + Makefile skeleton with `check-style` target.
- [x] First commit.

### Phase 1: timing infra + pointer chase + unit tests  (COMPLETE)

- [x] `bench_utils.hpp`: do_not_optimize / clobber_memory barriers, steady
  clock timer with repeat-and-take-minimum, dependency free JSON writer,
  aligned allocation.
- [x] `measure_clock_ghz_calibrated`: software clock via a dependent add chain.
  Necessary because WSL2 `/proc/cpuinfo` is pinned at 3.42 GHz and never tracks
  turbo (see engineering log). Calibration reports 5.36 GHz under load.
- [x] `topology.hpp`: sysfs cache parse (configurable root), hybrid P/E split
  detection with a documented heuristic fallback under WSL, thread pinning.
- [x] `progress.hpp`: TTY aware bar, plain lines under CI.
- [x] `perf_event.hpp`: compiled, gated to bare metal, `is_available()` false
  on WSL.
- [x] `pointer_chase`: 64 byte single cycle permutation chase, sweep 4 KB to
  512 MB, JSON out with per point ns and cycles.
- [x] Unit tests: `test_permutation` (single cycle for every swept size, plus
  negative controls) and `test_topology` (flat and hybrid fixtures). Both green.

**Measured plateau sanity (mini run, CPU pinned, calibrated 5.36 GHz):**

| Region | Working set | Latency | Cycles | Expectation |
|---|---|---|---|---|
| L1D | 4 to 32 KiB | 0.94 ns | 5.0 | Raptor Cove L1D is 5 cycles. PASS |
| L2 | 64 to 256 KiB | 3.03 ns | 16.3 | Raptor Cove L2 about 16 cycles. PASS |
| L3 | 1 to 8 MiB | 4 to 30 ns | 22 to 163 | rising through the 33 MB L3. PASS |
| DRAM | 16 to 64 MiB | 85 to 89 ns | ~460 | DDR5 random plus TLB walk. Gate 50 to 120 ns. PASS |

Both Phase 1 sanity gates (L1 3 to 7 cycles, DRAM 50 to 120 ns) pass on real
hardware.

### Findings so far (engineering log has full entries)

1. WSL2 does not expose the hybrid P/E split; parser falls back to the Intel
   enumeration convention and flags it as not measured.
2. WSL2 `/proc/cpuinfo` clock is fixed at base and ignores turbo; replaced with
   a software dependency chain calibration. This is the platform's version of
   the mandatory "compiler deleted my loop" fight.

### Phase 2: STREAM variants + scaling + plateau detection  (COMPLETE)

- [x] `stream_kernels.hpp`: Copy, Scale, Add, Triad in three variants (scalar
  with per function vectorization suppression, compiler vectorized, AVX2
  intrinsic with `_mm256_stream_ps` non temporal stores). Classical STREAM byte
  accounting.
- [x] `stream.cpp`: pinned barrier synchronized thread pool with first touch,
  single thread variant comparison, and a 1 to N thread scaling sweep on the NT
  variant. Rewrote the timing to live entirely inside workers (worker 0 reduces
  to the slowest thread per trial) after a main thread barrier race produced
  impossible multi thousand GB/s readings.
- [x] `test_stream_verify`: all three variants of all four kernels numerically
  correct on aligned buffers, built with native flags so the real AVX2 path is
  tested.
- [x] `plateau_detect.py`: two segment least squares change point detection via
  binary segmentation in log latency space, plus a sysfs cross check.
- [x] `test_plateau.py` (ctest integration): recovers known break points from a
  synthetic noisy staircase. Caught a real log versus raw SSE mixing bug.

**Measured STREAM (float32, calibrated clock, arrays 4x L3):**

| Variant | Copy | Scale | Add | Triad | (single thread GB/s) |
|---|---|---|---|---|---|
| scalar | 42.6 | 23.2 | 27.4 | 27.1 | honest scalar baseline |
| vec | 40.8 | 24.4 | 28.7 | 31.9 | compiler auto vectorized |
| nt | 36.6 | 35.8 | 37.1 | 37.5 | NT stores win on add and triad |

Thread scaling saturates near 78 GB/s by 4 to 5 threads (87 percent of the
DDR5-5600 dual channel 89.6 GB/s theoretical). The reported knee is the
bandwidth wall: memory saturates long before the cores do, so spilling from P
to E cores adds nothing. Single thread DRAM bandwidth sanity gate (30 to 65
GB/s) passes (copy 36 to 42).

**Plateau detection on the real curve:** L1D boundary 0.08 octaves from 48 KB,
L2 boundary 0.50 octaves from 2 MB (both pass); the DRAM onset is flagged about
1.5 octaves below the 33 MB L3, a real random access effect (finding, logged).

### Phase 3: peak FLOPS + sanity gates + roofline  (COMPLETE)

- [x] `flops_kernel.hpp`: ten independent AVX2 FMA chains (NEON and scalar
  variants included for the Phase 4 port). Non unity contractive multiplier
  prevents strength reduction and keeps values finite.
- [x] `peak_flops.cpp`: single P core and all core throughput, single core
  ceiling computed from the measured clock (clock x 8 lanes x 2 x 2 ports).
- [x] Hardened the clock calibration (warmup then max of four passes) after a
  single shot reading of 3.81 GHz produced an impossible 138 percent efficiency.
- [x] `sanity_gates.py`: all four Section 10 gates, warn or strict modes.
- [x] `roofline.py` and `plot_style.py`: measured roofline with the suite's
  kernels placed, Okabe Ito colorblind safe palette.
- [x] `run_suite.sh` (resumable, env capture), `assemble_summary.py`,
  `gen_report_assets.py` (figures and LaTeX tables from summary.json only).

**Measured peak FLOPS (calibrated clock 5.34 GHz):**

| Configuration | GFLOPS | Ceiling | Efficiency |
|---|---|---|---|
| single P core | 169.5 | 170.9 | 99 percent |
| all core (28 threads) | 1915 | aggregate | |

Measured single core throughput matches the ceiling computed from the same
clock to within one percent, confirming both the clock and the FLOP count.

**Roofline:** peak compute 1915 GFLOPS, peak bandwidth 78 GB/s, ridge point
24.6 FLOP per byte. STREAM Triad sits on the memory bound slope, Peak FMA on the
compute ceiling.

**All four sanity gates PASS (strict):** L1 5.02 cycles, DRAM 93.4 ns, single
thread bandwidth 46.1 GB/s, single P core 169.5 GFLOPS.

**Measured wall clock (i7-14700K, WSL2):** full suite 44 seconds (the adaptive
per point timing keeps the latency sweep stable without the pessimistic 20 to
40 minute budget). Recorded here to replace the estimate.
