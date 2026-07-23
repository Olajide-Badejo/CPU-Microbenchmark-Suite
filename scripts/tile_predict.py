#!/usr/bin/env python3
"""BLIS style analytical GEMM tile prediction, validated against the empirical
sweep in summary.json's gemm block.

The BLIS framework (Van Zee and van de Geijn, ACM TOMS 2015; analytical model
in Low, Igual, Smith, Quintana-Orti, van de Geijn, ACM TOMS 2016) sizes the
cache blocks so that packed panels stay resident:
  Kc  a B micro-panel (Kc x Nr) plus an A micro-panel (Mr x Kc) plus the C
      micro-tile (Mr x Nr) coexist in L1.
  Mc  the A block (Mc x Kc) resides in L2 alongside the L1 resident panels.
  Nc  the B block (Kc x Nc) resides in L3.
One cache way per level is reserved for streaming (the (W - 1) / W factor), and
sizes are floored to a multiple of the register block.

This is a capacity model in the BLIS spirit; the exact BLIS constants assume a
packed GEBP micro-kernel with register blocks Mr, Nr. The validation kernel here
is a naive blocked GEMM with no packing and a full width inner loop, so its best
tile is expected to differ. Reporting that difference, with its cause, is the
deliverable (Section 4 objective 4: a prediction miss is a finding, not a hidden
failure), not a pass or fail assertion.
"""

from __future__ import annotations

import json
import sys

# Register block sizes for an AVX2 single precision micro-kernel. These are the
# BLIS model's assumption, stated explicitly.
MR = 8
NR = 8
SDATA = 4  # bytes per float32


def predict_tiles(cache: dict) -> dict:
    """Return the BLIS style (Mc, Kc, Nc) from measured cache geometry."""
    l1 = cache["l1d_bytes"]
    w1 = cache["l1d_ways"] or 8
    l2 = cache["l2_bytes"]
    w2 = cache["l2_ways"] or 8
    l3 = cache["l3_bytes"]
    w3 = cache["l3_ways"] or 8

    # Kc from L1: (Kc*Nr + Mr*Kc + Mr*Nr) * Sdata <= (w1-1)/w1 * L1.
    usable_l1 = (w1 - 1) / w1 * l1
    kc = (usable_l1 - MR * NR * SDATA) / ((NR + MR) * SDATA)
    kc = max(MR, int(kc // MR) * MR)

    # Mc from L2: Mc*Kc*Sdata <= (w2-1)/w2 * L2.
    usable_l2 = (w2 - 1) / w2 * l2
    mc = usable_l2 / (kc * SDATA)
    mc = max(MR, int(mc // MR) * MR)

    # Nc from L3: Kc*Nc*Sdata <= (w3-1)/w3 * L3.
    usable_l3 = (w3 - 1) / w3 * l3
    nc = usable_l3 / (kc * SDATA)
    nc = max(NR, int(nc // NR) * NR)

    return {"Mc": mc, "Kc": kc, "Nc": nc, "Mr": MR, "Nr": NR}


def validate(summary: dict) -> dict:
    gemm = summary.get("gemm")
    if not gemm:
        raise SystemExit("no gemm block in summary.json; run gemm_validate first")

    cache = {k: gemm[k] for k in
             ("l1d_bytes", "l1d_ways", "l2_bytes", "l2_ways", "l3_bytes", "l3_ways")}
    predicted = predict_tiles(cache)

    empirical = {
        "Mc": gemm["best_mc"], "Kc": gemm["best_kc"], "Nc": gemm["best_nc"],
        "gflops": gemm["best_gflops"],
    }

    # How far is the empirical best Kc from the prediction, in octaves? Kc is the
    # dimension the model most directly constrains.
    import math
    kc_octaves = abs(math.log2(empirical["Kc"] / predicted["Kc"]))
    hit = kc_octaves <= 1.0

    return {
        "predicted": predicted,
        "empirical_best": empirical,
        "kc_octaves_apart": kc_octaves,
        "prediction_hit": hit,
    }


def render_table(result: dict) -> str:
    p = result["predicted"]
    e = result["empirical_best"]
    finding = ("within one octave" if result["prediction_hit"]
               else "a miss of "
               f"{result['kc_octaves_apart']:.1f} octaves in Kc")
    return (
        "\\begin{tabular}{lrrr}\n\\hline\n"
        "Source & Mc & Kc & Nc \\\\\n\\hline\n"
        f"BLIS analytical & {p['Mc']} & {p['Kc']} & {p['Nc']} \\\\\n"
        f"Empirical best & {e['Mc']} & {e['Kc']} & {e['Nc']} \\\\\n"
        "\\hline\n\\end{tabular}\n"
        f"% Kc prediction is {finding}.\n"
    )


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print("usage: tile_predict.py summary.json [out_table.tex]", file=sys.stderr)
        return 2
    with open(argv[1], encoding="utf-8") as fh:
        summary = json.load(fh)
    result = validate(summary)

    p, e = result["predicted"], result["empirical_best"]
    print("BLIS analytical prediction (Mr=%d, Nr=%d):" % (p["Mr"], p["Nr"]))
    print(f"  Mc={p['Mc']}  Kc={p['Kc']}  Nc={p['Nc']}")
    print("Empirical best from the naive blocked sweep:")
    print(f"  Mc={e['Mc']}  Kc={e['Kc']}  Nc={e['Nc']}  ({e['gflops']:.1f} GFLOPS)")
    verdict = "HIT" if result["prediction_hit"] else "MISS (finding)"
    print(f"Kc agreement: {result['kc_octaves_apart']:.2f} octaves apart -> {verdict}")

    if len(argv) > 2:
        with open(argv[2], "w", encoding="utf-8") as fh:
            fh.write(render_table(result))
        print(f"wrote {argv[2]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
