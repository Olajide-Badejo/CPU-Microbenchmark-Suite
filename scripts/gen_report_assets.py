#!/usr/bin/env python3
"""Regenerate every report figure and table from summary.json.

Idempotent: given the same summary.json it produces byte stable inputs to the
report, so the PDFs always rebuild from the committed data. Figures go to
report/figures, LaTeX tables to report/tables. This is the only bridge from
measured numbers to the report, so a plot can never disagree with a table.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import matplotlib.pyplot as plt
from tqdm import tqdm

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))
from plateau_detect import (  # noqa: E402
    cross_check_sysfs,
    detect_change_points,
    plateaus_from_curve,
)
from plot_style import apply_style, color  # noqa: E402
from roofline import build_roofline  # noqa: E402
from tile_predict import render_table, validate  # noqa: E402

FIGDIR = ROOT / "report" / "figures"
TABDIR = ROOT / "report" / "tables"


def region_name(i: int, total: int) -> str:
    names = ["L1D", "L2", "L3", "DRAM"]
    if total <= len(names):
        return names[i] if i < len(names) else f"level {i + 1}"
    return f"plateau {i + 1}"


# --------------------------------------------------------------------------
# Figures
# --------------------------------------------------------------------------
def fig_latency(summary: dict) -> None:
    pc = summary["pointer_chase"]
    sizes = [p["bytes"] for p in pc["points"]]
    kib = [b / 1024 for b in sizes]
    lat = [p["latency_ns"] for p in pc["points"]]

    segs = plateaus_from_curve(sizes, lat)

    apply_style()
    fig, ax = plt.subplots(figsize=(7.2, 4.4))
    ax.plot(kib, lat, marker="o", color=color(0), label="Load use latency")

    # Plateau means as horizontal guides. Dark green for the plateau lines and
    # their labels so they read as one family, distinct from the data line. The
    # label sits just above its own dashed line (a small fixed offset in points,
    # not a multiplicative offset that would fling the DRAM label far up).
    dark_green = "#0b6b3a"
    for i, seg in enumerate(segs):
        x0, x1 = kib[seg.start], kib[seg.end - 1]
        ax.hlines(seg.mean, x0, x1, color=dark_green, linewidth=1.6,
                  linestyle="--", alpha=0.9)
        ax.annotate(f"{region_name(i, len(segs))} {seg.mean:.1f} ns",
                    xy=(x0, seg.mean), xytext=(2, 3),
                    textcoords="offset points", ha="left", va="bottom",
                    color=dark_green, fontsize=8, fontweight="bold")

    # sysfs capacities as vertical references.
    caps = {"L1D": pc.get("sysfs_l1d_bytes", 0),
            "L2": pc.get("sysfs_l2_bytes", 0),
            "L3": pc.get("sysfs_l3_bytes", 0)}
    for name, cap in caps.items():
        if cap > 0:
            ax.axvline(cap / 1024, color="#b0b0b0", linewidth=1.0, linestyle=":")
            ax.annotate(name, xy=(cap / 1024, min(lat)), color="#8a8a8a",
                        fontsize=8, rotation=90, va="bottom", ha="right")

    ax.set_xscale("log", base=2)
    ax.set_xlabel("Working set (KiB)")
    ax.set_ylabel("Latency (ns)")
    ax.set_title("Pointer chase load use latency, plateaus and sysfs capacities")
    ax.legend(loc="upper left")
    fig.tight_layout()
    fig.savefig(FIGDIR / "latency_plateau.png")
    plt.close(fig)


def fig_scaling(summary: dict) -> None:
    st = summary["stream"]
    sc = st.get("scaling")
    if not sc:
        return
    threads = sc["threads"]
    n_p = st.get("num_p_cores", 0)

    apply_style()
    fig, ax = plt.subplots(figsize=(7.2, 4.4))
    for i, k in enumerate(["copy", "scale", "add", "triad"]):
        ax.plot(threads, sc[k], marker="o", color=color(i), label=k.capitalize())

    # Shade and label the P core and E core thread ranges: P cores light grey,
    # E cores a lighter grey, so it is obvious which threads are which.
    lo, hi = 0.5, max(threads) + 0.5
    top = ax.get_ylim()[1]
    if n_p and n_p < max(threads):
        p_hi = n_p + 0.5
        ax.axvspan(lo, p_hi, color="#e6e6e6", zorder=0)
        ax.axvspan(p_hi, hi, color="#f4f4f4", zorder=0)
        ax.text((lo + p_hi) / 2, top * 0.97, "P cores", color="#6b6b6b",
                fontsize=9, ha="center", va="top")
        ax.text((p_hi + hi) / 2, top * 0.97, "E cores", color="#8a8a8a",
                fontsize=9, ha="center", va="top")
        ax.set_ylim(top=top)
        ax.set_xlim(lo, hi)

    ax.set_xlabel("Threads (P cores first, then E cores)")
    ax.set_ylabel("Bandwidth (GB/s)")
    ax.set_title("STREAM bandwidth scaling (NT stores), P to E knee")
    ax.legend(loc="lower right", ncol=2)
    fig.tight_layout()
    fig.savefig(FIGDIR / "bandwidth_scaling.png")
    plt.close(fig)


def fig_variants(summary: dict) -> None:
    st = summary["stream"].get("single_thread")
    if not st:
        return
    kernels = ["copy", "scale", "add", "triad"]
    variants = ["scalar", "vec", "nt"]
    labels = {"scalar": "Scalar", "vec": "Auto vectorized", "nt": "AVX2 NT"}

    apply_style()
    fig, ax = plt.subplots(figsize=(7.2, 4.4))
    width = 0.26
    for vi, v in enumerate(variants):
        xs = [k + (vi - 1) * width for k in range(len(kernels))]
        ys = [st[v][k] for k in kernels]
        ax.bar(xs, ys, width=width, color=color(vi), label=labels[v])

    ax.set_xticks(range(len(kernels)))
    ax.set_xticklabels([k.capitalize() for k in kernels])
    ax.set_ylabel("Bandwidth (GB/s)")
    ax.set_title("Single thread STREAM by variant")
    ax.legend(loc="upper right", ncol=3)
    ax.grid(axis="x", visible=False)
    fig.tight_layout()
    fig.savefig(FIGDIR / "variant_comparison.png")
    plt.close(fig)


# --------------------------------------------------------------------------
# LaTeX tables
# --------------------------------------------------------------------------
def _write(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8")


def tab_latency(summary: dict) -> None:
    pc = summary["pointer_chase"]
    sizes = [p["bytes"] for p in pc["points"]]
    lat = [p["latency_ns"] for p in pc["points"]]
    cyc = [p["cycles"] for p in pc["points"]]
    segs = plateaus_from_curve(sizes, lat)
    rows = []
    for i, seg in enumerate(segs):
        lo = sizes[seg.start] / 1024
        hi = sizes[seg.end - 1] / 1024
        mean_cyc = sum(cyc[seg.start:seg.end]) / (seg.end - seg.start)
        rows.append(f"{region_name(i, len(segs))} & {lo:.0f} to {hi:.0f} KiB & "
                    f"{seg.mean:.2f} & {mean_cyc:.1f} \\\\")
    body = "\n".join(rows)
    _write(TABDIR / "latency_table.tex",
           "\\begin{tabular}{llrr}\n\\hline\n"
           "Region & Working set & Latency (ns) & Cycles \\\\\n\\hline\n"
           f"{body}\n\\hline\n\\end{{tabular}}\n")


def tab_bandwidth(summary: dict) -> None:
    st = summary["stream"].get("single_thread")
    if not st:
        return
    rows = []
    for v, label in [("scalar", "Scalar"), ("vec", "Auto vectorized"),
                     ("nt", "AVX2 NT stores")]:
        r = st[v]
        rows.append(f"{label} & {r['copy']:.1f} & {r['scale']:.1f} & "
                    f"{r['add']:.1f} & {r['triad']:.1f} \\\\")
    body = "\n".join(rows)
    _write(TABDIR / "bandwidth_table.tex",
           "\\begin{tabular}{lrrrr}\n\\hline\n"
           "Variant & Copy & Scale & Add & Triad \\\\\n\\hline\n"
           f"{body}\n\\hline\n\\end{{tabular}}\n")


def tab_flops(summary: dict) -> None:
    pf = summary["peak_flops"]
    body = (
        f"Single P core & {pf['single_core_gflops']:.1f} & "
        f"{pf['ceiling_single_gflops']:.1f} & "
        f"{pf['single_core_efficiency'] * 100:.0f}\\% \\\\\n"
        f"All core ({pf['all_core_threads']} threads) & "
        f"{pf['all_core_gflops']:.0f} & (aggregate) & \\\\"
    )
    _write(TABDIR / "flops_table.tex",
           "\\begin{tabular}{lrrr}\n\\hline\n"
           "Configuration & GFLOPS & Ceiling & Efficiency \\\\\n\\hline\n"
           f"{body}\n\\hline\n\\end{{tabular}}\n")


def tab_reproducibility(summary: dict) -> None:
    m = summary.get("meta", {})
    rows = [
        ("Compiler", m.get("compiler", "n/a")),
        ("Kernel", m.get("kernel", "n/a")),
        ("WSL2", str(m.get("wsl", "n/a"))),
        ("Measured clock", f"{m.get('clock_ghz_measured', 0):.2f} GHz"),
        ("Git commit", m.get("git_commit", "n/a")),
    ]
    body = "\n".join(f"{k} & {v} \\\\" for k, v in rows)
    # The compiler and kernel strings can be long, so the value column is a
    # fixed width paragraph column that wraps rather than overflowing the page.
    _write(TABDIR / "repro_table.tex",
           "\\small\n\\begin{tabular}{l p{9cm}}\n\\hline\n"
           f"{body}\n\\hline\n\\end{{tabular}}\n")


def main(argv: list[str]) -> int:
    summary_path = Path(argv[1]) if len(argv) > 1 else (
        ROOT / "experiments" / "results" / "summary.json")
    with open(summary_path, encoding="utf-8") as fh:
        summary = json.load(fh)

    FIGDIR.mkdir(parents=True, exist_ok=True)
    TABDIR.mkdir(parents=True, exist_ok=True)

    tasks = [
        ("roofline figure", lambda: build_roofline(summary, FIGDIR / "roofline.png")),
        ("latency figure", lambda: fig_latency(summary)),
        ("scaling figure", lambda: fig_scaling(summary)),
        ("variant figure", lambda: fig_variants(summary)),
        ("latency table", lambda: tab_latency(summary)),
        ("bandwidth table", lambda: tab_bandwidth(summary)),
        ("flops table", lambda: tab_flops(summary)),
        ("repro table", lambda: tab_reproducibility(summary)),
    ]
    if summary.get("gemm"):
        tasks.append(("tile table", lambda: _write(
            TABDIR / "tile_table.tex", render_table(validate(summary)))))
    for _label, fn in tqdm(tasks, desc="report assets", disable=not sys.stderr.isatty()):
        fn()

    # Cross check note printed for the log.
    pc = summary["pointer_chase"]
    sizes = [p["bytes"] for p in pc["points"]]
    lat = [p["latency_ns"] for p in pc["points"]]
    caps = {"L1D": pc.get("sysfs_l1d_bytes", 0), "L2": pc.get("sysfs_l2_bytes", 0),
            "L3": pc.get("sysfs_l3_bytes", 0)}
    for rec in cross_check_sysfs(sizes, detect_change_points(lat), caps):
        print(f"boundary near {rec['nearest_level']} "
              f"({rec['octaves_from_level']:.2f} octaves), "
              f"ok={rec['within_tolerance']}")
    print(f"assets written to {FIGDIR} and {TABDIR}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
