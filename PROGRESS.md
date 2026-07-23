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
