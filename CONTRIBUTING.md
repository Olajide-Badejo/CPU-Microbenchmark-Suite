# Contributing

This is a personal project, but the rules that keep it honest are worth stating
for anyone reading or extending it.

## Ground rules

1. **No number without a run.** Nothing is quoted from a spec sheet. If you add
   a measurement, it comes from a benchmark that emits JSON into the summary.
2. **No en or em dashes anywhere**, and never a literal double hyphen in LaTeX
   prose. Run `python3 scripts/check_no_dashes.py` before committing; it also
   runs in the build and the report compile.
3. **LTO stays off for benchmark targets.** The build asserts this. Do not turn
   it on to "speed things up"; it will delete the loop being measured.
4. **State platform caveats.** If a number is hard to get right on this
   platform, document the difficulty and the workaround. Do not publish a
   plausible figure quietly.

## Before you commit

```sh
make build
make test          # all unit and integration tests must pass
make check-style   # dash check and ruff must be clean
make arm-selftest  # if you touched a compute kernel
```

## Style

- C++: `snake_case` functions and files, `PascalCase` types, `UPPER_SNAKE`
  constants. Every benchmark file opens with a comment stating what is measured,
  what is deliberately defeated, and what would invalidate the number.
- Python: `ruff` clean.
- Vector kernels compile with `-O3 -march=native`; scalar baselines add
  `-fno-tree-vectorize` and nothing else uses it.

## Adding a benchmark

1. Add the source under `src/` and register it with `add_benchmark` in
   `src/CMakeLists.txt` (or `add_scalar_benchmark` for a scalar baseline).
2. Emit a self describing JSON record with units in the field names
   (`latency_ns`, `bandwidth_gbs`, `clock_ghz_measured`).
3. Add a correctness test under `tests/` and register it with `add_test`.
4. Wire it into `scripts/run_suite.sh` and, if it produces a figure or table,
   into `scripts/gen_report_assets.py`.
