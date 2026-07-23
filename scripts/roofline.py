#!/usr/bin/env python3
"""Assemble the measured roofline from summary.json and place the suite's own
kernels on it.

The roofline (Williams, Waterman, Patterson, CACM 2009) bounds attainable
performance by min(peak compute, arithmetic intensity times peak bandwidth).
Both ceilings here are measured on this machine, never taken from a spec sheet:
peak compute is the all core FMA throughput from peak_flops, peak bandwidth is
the saturated STREAM triad from the scaling sweep. The ridge point, where the
two ceilings meet, is marked. Each kernel is placed at its arithmetic intensity
(FLOPs per byte of DRAM traffic) and its measured performance.

Kernels placed:
  STREAM Triad  a = b + q*c : 2 FLOP over 12 bytes, AI = 1/6, memory bound.
  Peak FMA                   : the compute ceiling itself, very high AI.
  Blocked GEMM (if present)  : high AI, expected near the compute ceiling.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import matplotlib.pyplot as plt

sys.path.insert(0, str(Path(__file__).resolve().parent))
from plot_style import apply_style, color  # noqa: E402


def saturated_bandwidth_gbs(stream: dict) -> float:
    """Peak sustained bandwidth: the maximum triad point in the scaling sweep."""
    triad = stream.get("scaling", {}).get("triad", [])
    return max(triad) if triad else 0.0


def build_roofline(summary: dict, out_path: Path) -> dict:
    stream = summary.get("stream", {})
    flops = summary.get("peak_flops", {})

    peak_gflops = flops.get("all_core_gflops", 0.0)
    single_gflops = flops.get("single_core_gflops", 0.0)
    single_ceiling = flops.get("ceiling_single_gflops", single_gflops)
    peak_bw = saturated_bandwidth_gbs(stream)
    if peak_gflops <= 0 or peak_bw <= 0:
        raise SystemExit("roofline needs peak_flops and stream in summary.json")

    ridge_ai = peak_gflops / peak_bw  # FLOP per byte where the ceilings meet

    # Kernels to place: (label, arithmetic_intensity, measured_gflops).
    points = []
    # STREAM triad: 2 FLOP per element, 12 bytes moved per element -> AI = 1/6.
    # Placed against the all core bandwidth ceiling (saturated, multi thread).
    triad_bw = saturated_bandwidth_gbs(stream)
    if triad_bw > 0:
        ai = 2.0 / 12.0
        points.append(("STREAM Triad", ai, ai * triad_bw))
    # Peak FMA on one core sits at its single core ceiling (high AI).
    if single_gflops > 0:
        points.append(("Peak FMA (1 core)", ridge_ai * 8.0, single_gflops))
    # Naive blocked GEMM (single core): high AI, but far below the single core
    # ceiling because it is unpacked. The gap is the point of the figure.
    gemm = summary.get("gemm")
    if gemm and "arithmetic_intensity" in gemm and "best_gflops" in gemm:
        points.append(("Blocked GEMM (1 core)", gemm["arithmetic_intensity"],
                       gemm["best_gflops"]))

    apply_style()
    fig, ax = plt.subplots(figsize=(7.0, 4.6))

    ai_lo, ai_hi = 1e-2, max(1e3, ridge_ai * 20)
    # Memory bound branch: perf = AI * peak_bw, up to the ridge.
    mem_x = [ai_lo, ridge_ai]
    mem_y = [ai_lo * peak_bw, ridge_ai * peak_bw]
    # Compute bound branch: flat at peak_gflops beyond the ridge.
    comp_x = [ridge_ai, ai_hi]
    comp_y = [peak_gflops, peak_gflops]

    ax.plot(mem_x, mem_y, color=color(0), linewidth=2.2)
    ax.plot(comp_x, comp_y, color=color(0), linewidth=2.2, label="Measured roofline")
    # Single core compute ceiling, so the single core kernels have a fair line.
    if single_ceiling > 0:
        ax.hlines(single_ceiling, single_ceiling / peak_bw, ai_hi,
                  color="#8a8a8a", linewidth=1.4, linestyle="--")
        ax.annotate(f"1 core ceiling {single_ceiling:.0f} GFLOPS",
                    xy=(ai_hi * 0.05, single_ceiling * 1.08), color="#6b6b6b",
                    fontsize=8, ha="left")
    ax.axvline(ridge_ai, color="#b0b0b0", linewidth=1.0, linestyle=":")
    ax.annotate(f"ridge {ridge_ai:.1f} FLOP/byte",
                xy=(ridge_ai, peak_gflops), xytext=(ridge_ai * 1.1, peak_gflops * 0.35),
                color="#6b6b6b", fontsize=8)

    ax.annotate(f"peak compute {peak_gflops:.0f} GFLOPS",
                xy=(ai_hi, peak_gflops), xytext=(ai_hi * 0.05, peak_gflops * 1.1),
                color="#6b6b6b", fontsize=8, ha="left")
    ax.annotate(f"peak bandwidth {peak_bw:.0f} GB/s",
                xy=(ai_lo * 3, ai_lo * 3 * peak_bw), color="#6b6b6b", fontsize=8,
                rotation=34, ha="left")

    for i, (label, ai, perf) in enumerate(points):
        c = color(i + 1)
        ax.scatter([ai], [perf], color=c, s=55, zorder=5, edgecolor="white",
                   linewidth=1.0)
        ax.annotate(label, xy=(ai, perf), xytext=(ai * 1.15, perf * 0.72),
                    color=c, fontsize=9, fontweight="bold")

    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("Arithmetic intensity (FLOP / byte)")
    ax.set_ylabel("Performance (GFLOPS)")
    ax.set_title("Measured roofline, Intel Core i7-14700K")
    ax.legend(loc="lower right")
    ax.set_xlim(ai_lo, ai_hi)
    fig.tight_layout()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out_path)
    plt.close(fig)

    return {
        "peak_gflops": peak_gflops,
        "peak_bandwidth_gbs": peak_bw,
        "ridge_ai_flop_per_byte": ridge_ai,
        "points": [{"label": p[0], "ai": p[1], "gflops": p[2]} for p in points],
    }


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print("usage: roofline.py summary.json [out.png]", file=sys.stderr)
        return 2
    with open(argv[1], encoding="utf-8") as fh:
        summary = json.load(fh)
    out = Path(argv[2]) if len(argv) > 2 else Path("report/figures/roofline.png")
    info = build_roofline(summary, out)
    print(f"wrote {out}")
    print(f"  peak compute   {info['peak_gflops']:.0f} GFLOPS")
    print(f"  peak bandwidth {info['peak_bandwidth_gbs']:.0f} GB/s")
    print(f"  ridge point    {info['ridge_ai_flop_per_byte']:.1f} FLOP/byte")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
