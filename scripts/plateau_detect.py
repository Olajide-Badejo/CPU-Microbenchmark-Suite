#!/usr/bin/env python3
"""Detect cache latency plateaus in the pointer chase curve.

The latency versus working set curve is a staircase: a flat plateau at each
cache level, with a step up at each level's capacity. We find the plateau
boundaries with two segment least squares change point detection, applied
recursively (binary segmentation). Four plateaus on one noisy desktop do not
justify anything heavier; the recursion stops when a split no longer reduces
the residual sum of squares by a meaningful fraction.

The detected boundaries are then cross checked against the sysfs reported cache
capacities (L1D, L2, L3): a boundary should fall near the working set where the
data stops fitting in a given level. A boundary that lands far from every sysfs
capacity is reported so a human can look, rather than being smoothed over.

Used by gen_report_assets.py for the plateau chart and by the plateau test.
"""

from __future__ import annotations

import math
from dataclasses import dataclass


@dataclass
class Segment:
    start: int          # inclusive index into the input arrays
    end: int            # exclusive
    mean: float         # mean latency over the segment
    sse: float          # residual sum of squares around the mean


def _segment(values: list[float], start: int, end: int) -> Segment:
    n = end - start
    if n <= 0:
        return Segment(start, end, 0.0, 0.0)
    mean = sum(values[start:end]) / n
    sse = sum((v - mean) ** 2 for v in values[start:end])
    return Segment(start, end, mean, sse)


def _best_split(values: list[float], start: int, end: int) -> tuple[int, float]:
    """Two segment least squares: the split point minimizing left SSE + right
    SSE over [start, end). Returns (split_index, combined_sse). A split needs at
    least one point on each side.
    """
    best_idx = -1
    best_sse = math.inf
    for split in range(start + 1, end):
        left = _segment(values, start, split)
        right = _segment(values, split, end)
        total = left.sse + right.sse
        if total < best_sse:
            best_sse = total
            best_idx = split
    return best_idx, best_sse


def detect_change_points(
    latencies: list[float],
    max_segments: int = 4,
    min_gain_frac: float = 0.03,
    log_space: bool = True,
) -> list[int]:
    """Return sorted change point indices (segment boundaries) via binary
    segmentation. Splits the segment with the largest SSE reduction until we
    reach max_segments or no split reduces SSE by at least min_gain_frac of the
    whole curve SSE.

    Segmentation runs on log(latency) by default. Cache plateaus differ by
    ratios (a few ns at L1, tens of ns at DRAM), so in raw ns the single
    DRAM step dominates the total variance and the smaller L1 and L2 steps
    fall below any global threshold. In log space the four steps are comparable
    and all are recovered.
    """
    n = len(latencies)
    if n < 2:
        return []
    if log_space:
        work = [math.log(v) if v > 0 else -30.0 for v in latencies]
    else:
        work = list(latencies)

    whole = _segment(work, 0, n)
    if whole.sse <= 0:
        return []

    segments = [(0, n, whole.sse)]
    change_points: list[int] = []

    while len(segments) < max_segments:
        # Find the segment whose best split yields the largest SSE reduction.
        best_gain = 0.0
        best = None
        for seg_i, (s, e, sse) in enumerate(segments):
            if e - s < 2:
                continue
            split, combined = _best_split(work, s, e)
            if split < 0:
                continue
            gain = sse - combined
            if gain > best_gain:
                best_gain = gain
                best = (seg_i, s, e, split)

        if best is None or best_gain < min_gain_frac * whole.sse:
            break

        seg_i, s, e, split = best
        # Store child SSE in the same space the gains are computed in (work),
        # not raw latencies, or later comparisons mix log and raw magnitudes.
        left = _segment(work, s, split)
        right = _segment(work, split, e)
        segments.pop(seg_i)
        segments.append((s, split, left.sse))
        segments.append((split, e, right.sse))
        change_points.append(split)

    return sorted(change_points)


def plateaus_from_curve(
    sizes_bytes: list[int],
    latencies_ns: list[float],
    max_segments: int = 4,
) -> list[Segment]:
    """Segment the curve and return the plateaus as Segments, annotated with
    their mean latency. Boundaries are between sizes_bytes[cp-1] and
    sizes_bytes[cp].
    """
    cps = detect_change_points(latencies_ns, max_segments=max_segments)
    bounds = [0, *cps, len(latencies_ns)]
    out = []
    for i in range(len(bounds) - 1):
        out.append(_segment(latencies_ns, bounds[i], bounds[i + 1]))
    return out


def cross_check_sysfs(
    sizes_bytes: list[int],
    change_points: list[int],
    sysfs_caps: dict[str, int],
    tolerance_octaves: float = 1.0,
) -> list[dict]:
    """For each detected boundary, find the nearest sysfs cache capacity and
    report the distance in octaves (log2 ratio). A boundary is expected to sit
    just above the capacity of the level being exceeded. Returns one record per
    change point with the nearest level and whether it is within tolerance.
    """
    caps = [(name, cap) for name, cap in sysfs_caps.items() if cap > 0]
    records = []
    for cp in change_points:
        # Boundary sits between the last size that fit and the first that did
        # not; use the geometric mean of the two as the boundary location.
        lo = sizes_bytes[cp - 1]
        hi = sizes_bytes[cp]
        boundary = math.sqrt(lo * hi)
        nearest_name = None
        nearest_oct = math.inf
        for name, cap in caps:
            octaves = abs(math.log2(boundary / cap))
            if octaves < nearest_oct:
                nearest_oct = octaves
                nearest_name = name
        records.append(
            {
                "boundary_bytes": boundary,
                "between_kib": (lo / 1024, hi / 1024),
                "nearest_level": nearest_name,
                "octaves_from_level": nearest_oct,
                "within_tolerance": nearest_oct <= tolerance_octaves,
            }
        )
    return records


if __name__ == "__main__":
    import json
    import sys

    if len(sys.argv) < 2:
        print("usage: plateau_detect.py summary.json", file=sys.stderr)
        raise SystemExit(2)
    with open(sys.argv[1], encoding="utf-8") as fh:
        summary = json.load(fh)
    pc = summary["pointer_chase"]
    sizes = [p["bytes"] for p in pc["points"]]
    lat = [p["latency_ns"] for p in pc["points"]]
    cps = detect_change_points(lat)
    print(f"change points at indices: {cps}")
    for seg in plateaus_from_curve(sizes, lat):
        lo_kib = sizes[seg.start] / 1024
        hi_kib = sizes[seg.end - 1] / 1024
        print(f"  plateau {lo_kib:9.0f} to {hi_kib:9.0f} KiB : "
              f"{seg.mean:7.2f} ns")
    caps = {
        "L1D": pc.get("sysfs_l1d_bytes", 0),
        "L2": pc.get("sysfs_l2_bytes", 0),
        "L3": pc.get("sysfs_l3_bytes", 0),
    }
    for rec in cross_check_sysfs(sizes, cps, caps):
        print(f"  boundary near {rec['nearest_level']} "
              f"({rec['octaves_from_level']:.2f} octaves), "
              f"within_tolerance={rec['within_tolerance']}")
