# Engineering log

Dated entries, newest at the bottom. Each records symptom, root cause, options
weighed, the fix and why, and how it was verified. This is the raw record that
Phase 6 converts into the debug report PDF. The interesting fights live here.

## 2026-07-23  Phase 1  WSL2 hides the hybrid P/E split

**Symptom.** The topology reader could not tell P cores from E cores on the
target i7-14700K. The paths `/sys/devices/cpu_core/cpus` and
`/sys/devices/cpu_atom/cpus` that Linux uses to expose the hybrid split do not
exist under WSL2, and every logical CPU reports the same 2048 KB L2, so P and E
cannot be separated by cache size either.

**Root cause.** WSL2 runs under a Hyper-V utility VM that flattens the CPU
topology it presents to the guest. The hybrid enumeration is a bare metal
kernel feature that the virtualized `/sys` does not carry through.

**Options.** (1) Give up on P/E and pin to CPU 0 only. (2) Guess from CPU index
using the Intel enumeration convention. (3) Require bare metal.

**Fix and why.** `read_topology` detects the split when the sysfs files are
present and marks `hybrid_known = true`; when they are absent it falls back to
the documented Raptor Lake enumeration (P core SMT threads 0..15, E cores
16..27) and leaves `hybrid_known = false` so no caller can mistake the guess
for a measurement. The real split parsing path is still tested, against a
committed `sysfs_hybrid` fixture, so the bare metal code cannot rot.

**Verification.** `test_topology` passes both the flat (WSL style) and hybrid
(bare metal style) fixtures, asserting 16 P and 12 E cores in the hybrid case
and `hybrid_known == false` in the flat case.

## 2026-07-23  Phase 1  /proc/cpuinfo clock does not track turbo under WSL2

**Symptom.** The first pointer chase runs reported L1D latency at about 3.2
cycles. Raptor Cove L1D load to use latency is 5 cycles, so 3.2 was
impossible: the nanosecond timings were right (about 0.94 ns) but the cycle
conversion was too low.

**Root cause.** Cycles were computed from the `/proc/cpuinfo` "cpu MHz" field,
which under WSL2 is pinned to a fixed base value (measured 3417.601 MHz) and
never moves, even with four busy spinners loading the machine. There is no
`cpufreq` sysfs to consult either. So the divisor was the base clock while the
pinned core was actually boosting to about 5.4 GHz, and 0.94 ns at 5.4 GHz is
the expected 5.1 cycles.

**Options.** (1) Report the base clock and accept systematically low cycle
numbers with a caveat. (2) Back the clock out of an assumed L1 latency
(circular). (3) Measure the running clock in software with a calibrated
dependent instruction chain.

**Fix and why.** Added `measure_clock_ghz_calibrated`: it times a two billion
iteration dependent integer add chain, where each add has one cycle latency and
depends on the previous result, so the loop is latency bound at essentially one
cycle per iteration and frequency is simply iterations over elapsed time. No
PMU, no sysfs, and it follows turbo up to the real boost clock. The (wrong)
cpuinfo value is still recorded as `clock_ghz_cpuinfo` for transparency.

**Verification.** Calibration reports 5.36 GHz under load. With that divisor,
L1D reads 5.0 cycles, L2 16.3 cycles, DRAM 85 to 89 ns: all in the physically
expected ranges and inside the Section 10 sanity gates (L1 3 to 7 cycles, DRAM
50 to 120 ns). The cpuinfo value stays 3.42 GHz, confirming the gap.

## 2026-07-23  Phase 2  plateau detector mixed log and raw SSE

**Symptom.** The synthetic curve test failed: with four injected plateaus at 1,
3, 12, 90 ns the detector returned change points at indices 8, 13, 14 instead
of the true 4, 8, 13. It split the flat DRAM plateau at index 14 rather than
the obvious L1 to L2 step at index 4.

**Root cause.** Change point gains were computed in log(latency) space (the
right space, so the small L1 step and the large DRAM step are comparable), but
when a chosen segment was split, the two child segments had their stored SSE
recomputed from raw latencies. A DRAM child then carried a raw space SSE of
about 65 while every candidate gain was a log space quantity near 2, so on the
next iteration `gain = raw_sse - log_combined` produced a spurious gain of about
65 for splitting the DRAM plateau, which beat the real L1 to L2 split.

**Options.** (1) Do everything in raw space (loses the small steps). (2) Do
everything in log space (correct). (3) Keep two SSE spaces and convert (needless
complexity).

**Fix and why.** Compute the child segment SSE from `work` (the log transformed
array) exactly like the gains, so all magnitudes live in one space. One line.

**Verification.** `test_plateau` now recovers change points 4, 8, 13 within one
index and plateau means within 25 percent of the injected levels. On the real
machine curve the detector places the L1D boundary 0.08 octaves from the 48 KB
sysfs capacity and the L2 boundary 0.50 octaves from 2 MB.

## 2026-07-23  Phase 2  empirical DRAM onset sits below nominal L3 capacity

**Symptom.** Not a bug, a finding the cross check surfaced. The detected DRAM
plateau begins near an 8 to 16 MB working set, but sysfs reports a 33 MB L3, so
the boundary is about 1.5 octaves below the L3 capacity and the cross check
flags it as `within_tolerance = false`.

**Root cause.** Single threaded random pointer chasing does not reach the full
33 MB of shared L3 before latency climbs to DRAM levels: the random 64 byte
stride thrashes the TLB (4 KB pages give limited reach) and stresses L3
associativity and replacement, so effective capacity under this access pattern
is roughly half the nominal size. This is expected microarchitectural
behavior, documented in Drepper.

**Fix and why.** None. The cross check is meant to flag exactly this so a human
reads it. Reported in the results rather than smoothed over, per Section 10.

## 2026-07-23  Phase 3  measured FLOPS exceeded its own ceiling

**Symptom.** peak_flops reported 168 GFLOPS on one core against a computed
ceiling of 122 GFLOPS: an impossible 138 percent efficiency. The pointer chase
had reported a clean 5.36 GHz from the same calibration, but this run's
calibration returned 3.81 GHz.

**Root cause.** The clock calibration was a single shot: one timed pass of the
dependent add chain. That pass is only a lower bound on frequency, because any
deschedule, interrupt, or a core still ramping to its boost P-state only
lengthens the time and lowers the apparent clock. A single sample landed at
3.81 GHz while the FMA loop itself ran at about 5.3 GHz, so the ceiling (built
from the low clock) came out below the true throughput.

**Options.** (1) Average several passes (still dragged down by slow samples).
(2) Take the maximum of several passes after a warmup (the fastest pass is the
least perturbed and closest to the true clock). (3) Read an MSR (not available
under WSL).

**Fix and why.** measure_clock_ghz_calibrated now runs one warmup pass to bring
the core to its boost P-state, then takes the maximum apparent frequency over
four passes. The add chain cannot run faster than one cycle per iteration, so
the maximum is the cleanest estimate and cannot overshoot the true clock.

**Verification.** Three consecutive runs report 5.26, 5.33, 5.26 GHz, and the
single core FMA throughput sits at 99 to 100 percent of the ceiling computed
from that same clock. Matching the ceiling to within one percent is the
expected result for a saturating ten chain FMA loop and confirms both the clock
and the FLOP counting.
