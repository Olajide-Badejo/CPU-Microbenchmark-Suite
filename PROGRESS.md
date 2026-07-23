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
- [ ] CMake + Makefile skeleton with `check-style` target.
- [ ] First commit.
