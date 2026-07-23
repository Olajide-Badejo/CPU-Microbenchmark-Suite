# MicroArchBench

Measures what spec sheets only claim: pointer chase cache latencies, STREAM
bandwidth, and peak AVX2 and NEON FLOPS on a measured clock, assembled into a
measured roofline and a BLIS style GEMM tile prediction validated empirically.

Everything here is measured on real hardware, an Intel Core i7-14700K running
inside WSL2. No number is quoted from a data sheet. Where the platform makes a
number hard to get right, the difficulty and the workaround are stated rather
than a plausible figure quietly published.

## Headline results

| Quantity | Measured | Cross check |
|---|---|---|
| L1D load use latency | 0.94 ns (5.0 cycles) | Raptor Cove L1 is 5 cycles |
| L2 latency | 16.3 cycles | Raptor Cove L2 about 16 cycles |
| DRAM latency | 93 ns | DDR5 random plus TLB walk |
| Sustained bandwidth | 78 GB/s | 87% of DDR5-5600 dual channel peak |
| Single P core peak SP | 169 GFLOPS | 99% of the ceiling from the measured clock |
| All core peak SP | 1915 GFLOPS | 28 logical CPUs |

![Cache latency plateaus](report/figures/latency_plateau.png)

![Measured roofline](report/figures/roofline.png)

## The WSL2 measurement honesty note

WSL2 exposes no hardware performance counters, so every number is derived from
timing and a known transfer size (the classical STREAM approach). Two platform
facts drove the design:

1. `/proc/cpuinfo` reports a fixed base clock of 3.42 GHz and never tracks the
   real boost frequency (about 5.3 GHz), and there is no cpufreq interface. The
   suite measures the running clock in software with a dependent add chain, and
   every cycle denominated figure uses that measured clock.
2. The hybrid performance and efficiency core split is invisible to the guest,
   so P and E identification falls back to the Intel enumeration and is flagged
   as heuristic, not measured.

Both are documented in the [methodology](docs/methodology.md) and the
[engineering log](docs/ENGINEERING_LOG.md).

## What it measures

- **Pointer chase latency**: a prefetcher defeating single cycle random
  permutation at 64 byte stride, swept from 4 KB to 512 MB, with change point
  detection cross checked against sysfs cache sizes.
- **STREAM bandwidth**: Copy, Scale, Add, Triad in three variants (honest
  scalar, compiler vectorized, AVX2 with non temporal stores) plus a 1 to N
  thread scaling curve.
- **Peak FLOPS**: ten independent AVX2 FMA chains, single P core and all core,
  checked against the ceiling from the measured clock.
- **Roofline and tile prediction**: the measured ceilings assembled into a
  roofline, with a BLIS style analytical tile prediction validated against a
  blocked GEMM sweep. The prediction misses the naive kernel, and that miss is
  reported as a finding.
- **AArch64 NEON port**: FLOPS and STREAM kernels, correctness checked under
  qemu-user, real numbers from a native ARM CI runner.

## Build and run

Inside WSL2 Ubuntu (or any Linux):

```sh
make build      # configure and compile all benchmarks and tests
make test       # unit and integration tests (about 5 seconds)
make suite      # full measurement suite (about 45 seconds on the target)
make plots      # regenerate figures and tables from summary.json
make report     # build the main report PDF
make all        # clean tree reproduction of everything
```

The AArch64 NEON self test under qemu:

```sh
make arm-selftest
```

## Reports

- Main technical report: `report/build/main.pdf`
- Engineering debug report: `report_debug/build/debug_report.pdf`
- Personal documentation: `report_for_me/build/report_for_me.pdf`

## Measured wall clock

On the target i7-14700K under WSL2, the full suite runs in about 45 seconds. The
adaptive per point timing keeps the latency sweep stable without a long runtime.

## License

MIT, sole author Olajide Badejo.
