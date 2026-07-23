# Changelog

All notable changes to this project. Format loosely follows Keep a Changelog.

## 1.0.0

First release. Measures cache latency, memory bandwidth, and peak SIMD FLOPS on
an Intel Core i7-14700K under WSL2, with an AArch64 NEON port.

### Added

- Pointer chase load use latency, 4 KB to 512 MB, single cycle permutation at 64
  byte stride, with log space change point plateau detection cross checked
  against sysfs.
- STREAM Copy, Scale, Add, Triad in scalar, compiler vectorized, and AVX2 non
  temporal store variants, with a pinned thread pool and a 1 to N scaling curve.
- Peak single precision FLOPS from ten independent FMA chains, single P core and
  all core, compared to the ceiling from a software measured clock.
- Measured roofline and a BLIS style analytical tile prediction validated
  against a blocked GEMM sweep.
- AArch64 NEON port of the FLOPS and STREAM kernels, correctness under qemu-user,
  native ARM CI.
- Software clock calibration to work around the WSL2 fixed base clock.
- Sanity gates, reproducibility recording, and a resumable suite runner.
- Main report, engineering debug report, and personal documentation PDFs.
- Continuous integration on x86 (build, test, style, qemu NEON self test, report
  compile) and native ARM (build, test, mini suite).

### Notes

- Toolchain substitutions from the target specification: g++ 15.2.0 for the specified 16.1
  (unreleased) and CMake 4.2.3 for 4.4. Both cover every feature used.
- The BLIS tile prediction misses the naive kernel by design; reported as a
  finding, not a failure.
