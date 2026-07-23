#!/usr/bin/env python3
"""Merge the per benchmark JSON files from one raw results directory into a
single summary.json, the sole input to all downstream plotting and tables.

A reproducibility block is attached (compiler, flags, kernel, WSL flag, power
state, measured clock, pinned core, git commit) so every figure can be traced
back to the exact conditions that produced it (Section 4 rule 2). Missing
benchmark files are recorded as absent rather than aborting, so a partial run
still yields a usable summary.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

BENCHMARKS = ["pointer_chase", "stream", "peak_flops", "gemm"]


def load_if_present(raw_dir: Path, name: str):
    path = raw_dir / f"{name}.json"
    if not path.exists():
        return None
    with path.open(encoding="utf-8") as fh:
        return json.load(fh)


def main(argv: list[str]) -> int:
    if len(argv) < 3:
        print("usage: assemble_summary.py RAW_DIR OUT_JSON [ENV_JSON]",
              file=sys.stderr)
        return 2
    raw_dir = Path(argv[1])
    out_path = Path(argv[2])
    env_path = Path(argv[3]) if len(argv) > 3 else raw_dir / "env.json"

    summary: dict = {}
    if env_path.exists():
        with env_path.open(encoding="utf-8") as fh:
            summary["meta"] = json.load(fh)
    else:
        summary["meta"] = {}

    present = []
    for name in BENCHMARKS:
        data = load_if_present(raw_dir, name)
        if data is not None:
            summary[name] = data
            present.append(name)

    summary["meta"]["benchmarks_present"] = present

    # Carry the measured clock up to the meta block if any benchmark has it,
    # so the reproducibility line does not depend on which benchmark ran.
    for name in present:
        clk = summary[name].get("clock_ghz_measured")
        if clk:
            summary["meta"].setdefault("clock_ghz_measured", clk)
            break

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", encoding="utf-8") as fh:
        json.dump(summary, fh, indent=2)
    print(f"wrote {out_path} with benchmarks: {', '.join(present) or 'none'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
