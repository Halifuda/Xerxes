#!/usr/bin/env python3
"""Redraw Figure 17 (footprint regimes) with the steady-state controlled-injection data.

Style is taken verbatim from Xerxes/output/plot_ae_figures_rev.py::plot_footprint so
that the new figure matches Figures 16 and 18 (fonts, colors, widths, legend handler).
"""
from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "output"))

import plot_ae_figures_rev as P  # noqa: E402

P.import_plotting()
P.setup_style()

from matplotlib.legend_handler import HandlerPatch  # noqa: E402
from matplotlib.patches import Rectangle  # noqa: E402


def square_handler(drop=0.10):
    """Square swatch, nudged down so that its centre lines up with the label text."""

    class SquarePatchHandler(HandlerPatch):
        def create_artists(self, legend, orig_handle, xdescent, ydescent, width, height, fontsize, trans):
            side = height * 0.80
            y = ydescent + (height - side) / 2 - drop * height
            return [
                Rectangle(
                    (xdescent, y),
                    side,
                    side,
                    facecolor=orig_handle.get_facecolor(),
                    edgecolor=orig_handle.get_edgecolor(),
                    linewidth=orig_handle.get_linewidth(),
                    transform=trans,
                )
            ]

    return SquarePatchHandler()


CSV_PATH = ROOT / "results" / "repro" / "s52_footprint_steady" / "results.csv"
PROFILES = ("steady_controlled_ops45000_stride45", "steady_grid_ops45000_stride45")
OUT_DIR = ROOT.parent / "revision" / "figures-rev"
REGIME_OF = {16384: "single-endpoint regime", 131072: "leaf-group regime", 1048576: "beyond-leaf/global regime"}


def main() -> None:
    global CSV_PATH, OUT_DIR
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-root", type=Path, default=None,
                        help="s52_footprint_steady directory (contains results.csv)")
    parser.add_argument("--out-dir", type=Path, default=None)
    args = parser.parse_args()
    if args.data_root is not None:
        CSV_PATH = args.data_root / "results.csv"
    if args.out_dir is not None:
        OUT_DIR = args.out_dir
    rows = list(csv.DictReader(CSV_PATH.open(newline="")))
    keep = []
    for row in rows:
        if row["profile"] not in PROFILES:
            continue
        row = dict(row)
        row["regime"] = REGIME_OF[int(row["footprint_bytes"])]
        keep.append(row)

    layouts = ["no_interleave", "leaf_4way", "global_16way"]
    regimes = [
        ("single-endpoint regime", "Endpoint"),
        ("leaf-group regime", "Leaf"),
        ("beyond-leaf/global regime", "Global"),
    ]
    by_key = {(r["regime"], r["layout"]): r for r in keep}
    base_latency = P.num(by_key[(regimes[0][0], layouts[0])], "host_weighted_avg_latency_ns")
    x = P.np.arange(len(regimes))
    width = 0.24

    fig, axes = P.plt.subplots(1, 2, figsize=P.FIGSIZE)
    for offset, layout in zip([-width, 0.0, width], layouts):
        data = [by_key[(regime, layout)] for regime, _ in regimes]
        lat = P.norm([P.num(r, "host_weighted_avg_latency_ns") for r in data], base_latency)
        touched = [P.num(r, "sanity_mean_touched_endpoints") for r in data]
        for ax, y in ((axes[0], lat), (axes[1], touched)):
            ax.bar(
                x + offset,
                y,
                color=P.LAYOUT_COLORS[layout],
                edgecolor="black",
                linewidth=0.8,
                width=width,
                label=P.LAYOUT_LABELS[layout],
            )

    # value labels on the touched-endpoint panel only, as in the original figure;
    # a regime whose three bars share the same value gets one centred label
    for xi, (regime, _) in enumerate(regimes):
        touched = [P.num(by_key[(regime, layout)], "sanity_mean_touched_endpoints") for layout in layouts]
        if len(set(touched)) == 1:
            axes[1].text(xi, touched[0] + 0.7, f"{touched[0]:.0f}", ha="center", va="bottom", fontsize=10, color="black")
            continue
        for offset, value in zip([-width, 0.0, width], touched):
            axes[1].text(xi + offset, value + 0.6, f"{value:.0f}", ha="center", va="bottom", fontsize=10, color="black")

    labels = [label for _, label in regimes]
    for ax, ylabel in [(axes[0], "Norm. latency"), (axes[1], "Touched EPs")]:
        ax.set_xticks(x, labels)
        ax.set_ylabel(ylabel)
        P.polish(ax)
        ax.tick_params(axis="x", labelsize=11)
    axes[0].yaxis.set_label_coords(-0.23, 0.35)
    axes[1].yaxis.set_label_coords(-0.24, 0.35)
    axes[0].set_ylim(0, 1.5)
    axes[0].set_yticks([0.0, 0.5, 1.0, 1.5])
    axes[1].set_ylim(0, 20)
    axes[1].set_yticks([0, 5, 10, 15])

    handles = [
        P.Patch(facecolor=P.LAYOUT_COLORS[k], edgecolor="black", linewidth=0.8, label=P.LAYOUT_LABELS[k])
        for k in layouts
    ]
    fig.legend(
        handles,
        [P.LAYOUT_LABELS[k] for k in layouts],
        frameon=False,
        loc="upper right",
        bbox_to_anchor=(0.985, 0.965),
        ncol=3,
        prop={"size": 13},
        handlelength=1.15,
        handleheight=1.08,
        handletextpad=0.35,
        columnspacing=0.7,
        borderaxespad=0.0,
        borderpad=0.15,
        handler_map={P.Patch: square_handler(0.10)},
    )
    P.finish_two_panel(fig)
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    fig.savefig(OUT_DIR / "fig0609_footprint_regimes.eps", dpi=600, facecolor="white")
    fig.savefig(OUT_DIR / "fig0609_footprint_regimes.png", dpi=300, facecolor="white")
    print("wrote", OUT_DIR / "fig0609_footprint_regimes.eps")


if __name__ == "__main__":
    main()
