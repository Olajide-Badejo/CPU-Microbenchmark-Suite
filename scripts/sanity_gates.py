#!/usr/bin/env python3
"""Check measured results against the Section 10 sanity gates.

The gates are plausibility ranges derived from the documented hardware, not
correctness assertions. A value outside a gate is a warning to investigate, not
proof of a bug: the protocol is to flag rather than silently publish. By default
this prints a report and exits 0; with --strict it exits non zero if any gate is
breached, which is what CI uses to force a human look before a number ships.

Gates (Section 10):
  L1 latency                3 to 7 cycles
  DRAM latency              50 to 120 ns
  single thread DRAM BW     30 to 65 GB/s
  single P core peak SP     100 to 200 GFLOPS
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from plateau_detect import plateaus_from_curve  # noqa: E402

GATES = {
    "l1_cycles": (3.0, 7.0),
    "dram_ns": (50.0, 120.0),
    "single_thread_dram_gbs": (30.0, 65.0),
    "single_p_core_gflops": (100.0, 200.0),
}


def extract_metrics(summary: dict) -> dict:
    metrics: dict = {}

    pc = summary.get("pointer_chase")
    if pc:
        sizes = [p["bytes"] for p in pc["points"]]
        lat = [p["latency_ns"] for p in pc["points"]]
        cyc = [p["cycles"] for p in pc["points"]]
        segs = plateaus_from_curve(sizes, lat, max_segments=4)
        if segs:
            # First plateau is L1; use its mean cycles.
            l1 = segs[0]
            metrics["l1_cycles"] = sum(cyc[l1.start:l1.end]) / (l1.end - l1.start)
            # Last plateau is DRAM; use its mean ns.
            dram = segs[-1]
            metrics["dram_ns"] = dram.mean

    st = summary.get("stream")
    if st and "single_thread" in st:
        # Copy is the classical single thread bandwidth figure. Take the best
        # of the three variants (the honest achievable single thread number).
        copies = [st["single_thread"][v]["copy"] for v in ("scalar", "vec", "nt")]
        metrics["single_thread_dram_gbs"] = max(copies)

    pf = summary.get("peak_flops")
    if pf:
        metrics["single_p_core_gflops"] = pf.get("single_core_gflops")

    return metrics


def main(argv: list[str]) -> int:
    strict = "--strict" in argv
    args = [a for a in argv[1:] if not a.startswith("--")]
    if not args:
        print("usage: sanity_gates.py summary.json [--strict]", file=sys.stderr)
        return 2
    with open(args[0], encoding="utf-8") as fh:
        summary = json.load(fh)

    metrics = extract_metrics(summary)
    breaches = 0
    print("Sanity gates (Section 10):")
    for key, (lo, hi) in GATES.items():
        val = metrics.get(key)
        if val is None:
            print(f"  {key:26s} : no data")
            continue
        ok = lo <= val <= hi
        status = "PASS" if ok else "INVESTIGATE"
        if not ok:
            breaches += 1
        print(f"  {key:26s} : {val:8.2f}  [{lo:g}, {hi:g}]  {status}")

    if breaches:
        print(f"\n{breaches} gate(s) breached. Investigate before publishing.",
              file=sys.stderr)
        return 1 if strict else 0
    print("\nall gates within range.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
