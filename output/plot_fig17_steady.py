#!/usr/bin/env python3
"""Fig. 17 (footprint regimes) -- steady-state version.

Left panel : host-weighted average latency normalized to the 45k 16 KiB Non cell.
Right panel: mean number of touched endpoints.
Style follows Xerxes/output/plot_repro_final.py: Helvetica, no grid, dashed
baseline at 1.0, thin black bar edges, value labels above the bars.
"""
from __future__ import annotations

import argparse
import csv
import statistics
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Patch

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CSV = ROOT / "results" / "repro" / "s52_footprint_steady" / "results.csv"
DEFAULT_OUT = ROOT / "results" / "repro" / "s52_footprint_steady" / "fig17_steady.png"

LAYOUTS = ("no_interleave", "leaf_4way", "global_16way")
LABEL = {"no_interleave": "Non", "leaf_4way": "Group", "global_16way": "Flat"}
COLOR = {"no_interleave": "#C0C0C0", "leaf_4way": "#00C8C8", "global_16way": "#00A0FF"}
REGIMES = ((16 * 1024, "Endpoint\n16 KiB"), (128 * 1024, "Leaf\n128 KiB"), (1 << 20, "Global\n1 MiB"))


def setup_style() -> None:
    plt.rcParams.update(
        {
            "font.family": "Helvetica",
            "font.size": 9,
            "axes.labelsize": 11,
            "axes.titlesize": 11,
            "xtick.labelsize": 9.5,
            "ytick.labelsize": 10,
            "legend.fontsize": 10,
            "axes.linewidth": 0.8,
            "xtick.major.width": 0.8,
            "ytick.major.width": 0.8,
            "xtick.major.size": 4,
            "ytick.major.size": 4,
            "figure.dpi": 150,
            "savefig.dpi": 300,
            "axes.grid": False,
            "pdf.use14corefonts": True,
        }
    )


def load(path: Path, experiment: str) -> dict[tuple[int, str], list[dict[str, str]]]:
    cells: dict[tuple[int, str], list[dict[str, str]]] = {}
    with path.open(newline="") as fh:
        for row in csv.DictReader(fh):
            if row.get("experiment") != experiment or row.get("run_status") != "ok":
                continue
            key = (int(float(row["footprint_bytes"])), row["layout"])
            cells.setdefault(key, []).append(row)
    return cells


def stat(rows: list[dict[str, str]], col: str) -> tuple[float, float]:
    vals = [float(r[col]) for r in rows if r.get(col) not in ("", None)]
    if not vals:
        return float("nan"), 0.0
    return statistics.fmean(vals), (max(vals) - min(vals)) / 2.0


def default_footnote(csv_path: Path, experiment: str) -> str:
    """Describe the run behind the bars, using completion.csv when available."""
    comp = csv_path.parent / "completion.csv"
    stride, ratio, complete = "", "", ""
    if comp.exists():
        with comp.open(newline="") as fh:
            rows = [r for r in csv.DictReader(fh) if r.get("experiment") == experiment]
        strides = {r["stride"] for r in rows}
        ratios = [float(r["last_first"]) for r in rows if r.get("last_first") not in ("", "-")]
        complete = "yes" if rows and all(r.get("complete") == "yes" for r in rows) else "no"
        stride = "/".join(sorted(strides))
        if ratios:
            ratio = f"{max(ratios):.2f}"
    if experiment == "steady_controlled":
        return (
            f"tick_stride={stride}, ops_per_host=45000, max_clock=3e7, seed_base=10000; "
            f"trace consumed to the end ({complete} for all 9 cells), max last/first tenth = {ratio}."
        )
    if experiment == "steady_grid":
        return "Flat bars are window means of a non-stationary run at tick_stride 30 (see summary.md, item e)."
    return ""


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", type=Path, default=DEFAULT_CSV)
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--experiment", default="steady_controlled")
    ap.add_argument("--footnote", default="")
    args = ap.parse_args()

    setup_style()
    cells = load(args.csv, args.experiment)
    if not cells:
        raise SystemExit(f"no rows for experiment={args.experiment} in {args.csv}")
    base, _ = stat(cells[(16 * 1024, "no_interleave")], "host_weighted_avg_latency_ns")

    fig, (ax_l, ax_r) = plt.subplots(1, 2, figsize=(7.4, 2.25))
    width = 0.24
    xs = range(len(REGIMES))
    peak = 0.0

    for i, layout in enumerate(LAYOUTS):
        xs_i = [x + (i - 1) * width for x in xs]
        lat_mean, lat_err, touch_mean, touch_err = [], [], [], []
        for fp, _ in REGIMES:
            rows = cells.get((fp, layout), [])
            m, e = stat(rows, "host_weighted_avg_latency_ns")
            lat_mean.append(m / base)
            lat_err.append(e / base)
            tm, te = stat(rows, "sanity_mean_touched_endpoints")
            touch_mean.append(tm)
            touch_err.append(te)
        peak = max([peak] + lat_mean)
        for ax, mean, err, fmt in (
            (ax_l, lat_mean, lat_err, "{:.2f}\u00d7"),
            (ax_r, touch_mean, touch_err, "{:.0f}"),
        ):
            bars = ax.bar(
                xs_i,
                mean,
                width,
                color=COLOR[layout],
                edgecolor="black",
                linewidth=0.6,
                zorder=3,
                label=LABEL[layout],
            )
            if max(err) > 1e-9:
                ax.errorbar(
                    xs_i,
                    mean,
                    yerr=[[min(e, m) for e, m in zip(err, mean)], err],
                    fmt="none",
                    ecolor="black",
                    elinewidth=0.6,
                    capsize=2,
                    capthick=0.6,
                    zorder=4,
                )
            pad = 1.4 if ax is ax_l else 3.0
            for rect, value in zip(bars, mean):
                ax.annotate(
                    fmt.format(value),
                    xy=(rect.get_x() + rect.get_width() / 2.0, rect.get_height()),
                    xytext=(0, pad),
                    textcoords="offset points",
                    ha="center",
                    va="bottom",
                    rotation=90 if ax is ax_l else 0,
                    fontsize=7.2,
                    annotation_clip=False,
                    zorder=5,
                )

    ax_l.axhline(1.0, color="#808080", linestyle=(0, (1.5, 4.0)), linewidth=0.8, zorder=2)
    ax_l.set_ylabel("Normalized latency")
    ax_l.set_ylim(0, peak * 1.24)
    ax_l.set_title("(a) Latency", pad=5)

    ax_r.axhline(1.0, color="#808080", linestyle=(0, (1.5, 4.0)), linewidth=0.8, zorder=2)
    ax_r.set_ylabel("Touched endpoints")
    ax_r.set_ylim(0, 18.8)
    ax_r.set_yticks([0, 4, 8, 12, 16])
    ax_r.set_title("(b) Footprint", pad=5)

    for ax in (ax_l, ax_r):
        ax.set_xticks(list(xs))
        ax.set_xticklabels([label for _, label in REGIMES], linespacing=1.15)
        ax.tick_params(axis="x", length=0)
        ax.spines["top"].set_visible(False)
        ax.spines["right"].set_visible(False)
        ax.grid(False)

    handles = [Patch(facecolor=COLOR[l], edgecolor="black", linewidth=0.6, label=LABEL[l]) for l in LAYOUTS]
    fig.legend(
        handles=handles,
        loc="upper center",
        ncol=3,
        frameon=False,
        bbox_to_anchor=(0.5, 1.03),
        handlelength=1.3,
        columnspacing=1.8,
        handletextpad=0.5,
    )
    footnote = args.footnote or default_footnote(args.csv, args.experiment)
    if footnote:
        fig.text(0.005, 0.005, footnote, fontsize=6.2, color="#606060", ha="left", va="bottom")
    fig.tight_layout(rect=(0, 0.035, 1, 0.90), w_pad=1.8)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.out, bbox_inches="tight", pad_inches=0.02)
    print(f"wrote {args.out}")

    for fp, label in REGIMES:
        line = []
        for layout in LAYOUTS:
            rows = cells.get((fp, layout), [])
            m, _ = stat(rows, "host_weighted_avg_latency_ns")
            tm, _ = stat(rows, "sanity_mean_touched_endpoints")
            line.append(f"{LABEL[layout]} {m / base:.3f}x/{tm:.2f}")
        print(f"  {label.replace(chr(10), ' '):18s} " + "  ".join(line))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
