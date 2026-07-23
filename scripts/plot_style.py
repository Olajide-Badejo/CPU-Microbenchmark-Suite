"""Shared plot styling so every report figure reads as one system.

The report PDFs are printed on white pages, so the figures commit to a single
light look. Series colors use the Okabe Ito palette, a colorblind safe
categorical set widely validated in the literature, assigned in fixed order so
identity never depends on rank. Grid and axes are recessive; marks are thin;
series are direct labeled where it does not crowd.
"""

from __future__ import annotations

import matplotlib as mpl

# Okabe Ito, fixed order. Never cycled past the end; a figure needing more than
# these folds categories instead.
OKABE_ITO = [
    "#0072B2",  # blue
    "#E69F00",  # orange
    "#009E73",  # green
    "#D55E00",  # vermillion
    "#CC79A7",  # reddish purple
    "#56B4E9",  # sky blue
    "#F0E442",  # yellow
    "#000000",  # black
]

INK = "#1a1a1a"
MUTED = "#6b6b6b"
GRID = "#d9d9d9"


def apply_style() -> None:
    mpl.rcParams.update({
        "figure.dpi": 130,
        "savefig.dpi": 160,
        "figure.facecolor": "white",
        "axes.facecolor": "white",
        "axes.edgecolor": MUTED,
        "axes.linewidth": 0.8,
        "axes.labelcolor": INK,
        "axes.titlesize": 12,
        "axes.titleweight": "bold",
        "axes.labelsize": 10,
        "axes.grid": True,
        "axes.axisbelow": True,
        "grid.color": GRID,
        "grid.linewidth": 0.6,
        "xtick.color": MUTED,
        "ytick.color": MUTED,
        "xtick.labelsize": 9,
        "ytick.labelsize": 9,
        "text.color": INK,
        "legend.frameon": False,
        "legend.fontsize": 9,
        "lines.linewidth": 2.0,
        "lines.markersize": 6,
        "font.family": "DejaVu Sans",
    })


def color(i: int) -> str:
    return OKABE_ITO[i % len(OKABE_ITO)]
