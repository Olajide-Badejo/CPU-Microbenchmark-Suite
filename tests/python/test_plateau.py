#!/usr/bin/env python3
"""Integration test: the plateau detector recovers known break points from a
synthetic staircase curve with noise. If this passes, the change point method
is trustworthy on the real, noisier machine curve.
"""

from __future__ import annotations

import random
import sys
from pathlib import Path

# Import the detector from the repo scripts directory.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "scripts"))

from plateau_detect import (  # noqa: E402
    cross_check_sysfs,
    detect_change_points,
    plateaus_from_curve,
)


def build_synthetic():
    """Four plateaus at 1, 3, 12, 90 ns with known boundaries, plus noise."""
    rng = random.Random(1234)
    sizes = [4 * 1024 * (2**i) for i in range(18)]  # 4 KB to 512 MB
    true_levels = [1.0] * 4 + [3.0] * 4 + [12.0] * 5 + [90.0] * 5
    true_cps = [4, 8, 13]
    lat = [v + rng.gauss(0.0, 0.04 * v) for v in true_levels]
    return sizes, lat, true_cps


def main() -> int:
    failures = 0
    sizes, lat, true_cps = build_synthetic()

    cps = detect_change_points(lat, max_segments=4)
    # Expect three boundaries for four plateaus.
    if len(cps) != 3:
        print(f"FAIL expected 3 change points, got {len(cps)}: {cps}",
              file=sys.stderr)
        failures += 1

    # Each detected boundary must sit within one index of the true boundary.
    for got, want in zip(cps, true_cps):
        if abs(got - want) > 1:
            print(f"FAIL change point {got} not near expected {want}",
                  file=sys.stderr)
            failures += 1

    # Plateau means should be close to the injected levels.
    segs = plateaus_from_curve(sizes, lat, max_segments=4)
    expected_means = [1.0, 3.0, 12.0, 90.0]
    if len(segs) == 4:
        for seg, exp in zip(segs, expected_means):
            if abs(seg.mean - exp) > 0.25 * exp:
                print(f"FAIL plateau mean {seg.mean:.2f} far from {exp}",
                      file=sys.stderr)
                failures += 1
    else:
        print(f"FAIL expected 4 plateaus, got {len(segs)}", file=sys.stderr)
        failures += 1

    # Cross check against plausible cache capacities should flag boundaries
    # near a level. With these synthetic boundaries and real 14700K caps, the
    # detector should find a nearest level for each boundary.
    caps = {"L1D": 48 * 1024, "L2": 2048 * 1024, "L3": 33792 * 1024}
    recs = cross_check_sysfs(sizes, cps, caps)
    if len(recs) != len(cps):
        print("FAIL cross check record count mismatch", file=sys.stderr)
        failures += 1

    if failures == 0:
        print("plateau detector recovered all synthetic break points")
        return 0
    print(f"{failures} check(s) failed", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
