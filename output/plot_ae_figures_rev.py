#!/usr/bin/env python3
"""Regenerate the six AE Section 5 figures from the revised (2026-10) datasets.

Differences from output/plot_ae_figures.py (the June version):
  * data sources are the re-run datasets (rev_52_rerun, _regime_p, _p64);
  * Section 5.4 figures read both the ClnEvct-on (_p64/_regime_p) and
    ClnEvct-off (_p64_off/_regime_p_off) roots when the off roots exist;
  * Figure 24 (mapping) now reports absolute latency and BISnp counts;
  * Figure 25 uses the ways axis (capacity ratio) with a log BISnp panel;
  * Figure 26 is the shared-ratio axis at P=64 with three-seed whiskers;
  * bar-top labels are emitted for every bar panel (the June final EPS had them,
    the checked-in plotter did not).
"""

from __future__ import annotations

import argparse
import csv
import logging
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT = ROOT.parent / "revision" / "figures-rev"
DEFAULT_EXT = ROOT / "results" / "ae_extension" / "ae_extension_results.csv"
DEFAULT_FOOTPRINT = ROOT / "results" / "0609_12_rerun" / "0609_12_plot_points.csv"
DEFAULT_REGIME = ROOT / "results" / "rev_54_wl2" / "_regime_p"
DEFAULT_CAP = ROOT / "results" / "rev_54_wl2" / "_p64" / "cap_p64_seeds3.csv"
DEFAULT_RATIO = ROOT / "results" / "rev_54_wl2" / "_p64" / "ratio_p64.csv"
DEFAULT_P64 = ROOT / "results" / "rev_54_wl2" / "_p64"
DEFAULT_P64_OFF = ROOT / "results" / "rev_54_wl2" / "_p64_off"
DEFAULT_REGIME_OFF = ROOT / "results" / "rev_54_wl2" / "_p64_off"
DEFAULT_P64_LEAF = ROOT / "results" / "rev_54_wl2" / "_p64_leaf"

FIGURE_NAMES = (
    "fig0609_placement_perfect",
    "fig0609_footprint_regimes",
    "fig0609_group_size_sensitivity",
    "fig0609_sf_placement_pressure",
    "fig0609_fs_capacity_window",
    "fig0609_fs_sharing_shape",
)

LAYOUT_LABELS = {
    "no_interleave": "Non",
    "leaf_4way": "Group",
    "global_16way": "Flat",
    "local_2way": "2-way",
    "crossleaf_8way": "8-way",
}
MAPPING_LABELS = {"non": "Non", "leaf": "Group", "global": "Flat"}
COLORS = {"non": "#C0C0C0", "group": "#00C8C8", "flat": "#00A0FF"}
MAPPING_COLORS = {"non": COLORS["non"], "leaf": COLORS["group"], "global": COLORS["flat"]}
LAYOUT_COLORS = {
    "no_interleave": COLORS["non"],
    "leaf_4way": COLORS["group"],
    "global_16way": COLORS["flat"],
}
FIGSIZE = (5.25, 1.80)
SEEDS = (10000, 20000, 30000)

mpl = None
plt = None
np = None
Patch = None
Rectangle = None


def import_plotting() -> None:
    global mpl, plt, np, Patch, Rectangle
    if mpl is not None:
        return
    import matplotlib as mpl_module
    import matplotlib.pyplot as plt_module
    import numpy as np_module
    from matplotlib.patches import Patch as PatchClass
    from matplotlib.patches import Rectangle as RectangleClass

    mpl, plt, np, Patch, Rectangle = (
        mpl_module,
        plt_module,
        np_module,
        PatchClass,
        RectangleClass,
    )


def square_patch_handler():
    from matplotlib.legend_handler import HandlerPatch

    class SquarePatchHandler(HandlerPatch):
        def create_artists(self, legend, orig_handle, xdescent, ydescent, width, height, fontsize, trans):
            side = height * 0.80
            y = ydescent + (height - side) / 2
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


def setup_style() -> None:
    logging.getLogger("matplotlib.font_manager").setLevel(logging.ERROR)
    mpl.rcParams.update(
        {
            "font.family": "Helvetica",
            "font.size": 10,
            "axes.labelsize": 13,
            "xtick.labelsize": 12,
            "ytick.labelsize": 12,
            "legend.fontsize": 13,
            "pdf.use14corefonts": True,
            "ps.useafm": True,
            "axes.linewidth": 0.8,
            "xtick.major.width": 0.8,
            "ytick.major.width": 0.8,
            "xtick.major.size": 4,
            "ytick.major.size": 4,
            "figure.dpi": 150,
        }
    )


def load_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="") as fh:
        return list(csv.DictReader(fh))


def num(row: dict[str, str], key: str) -> float:
    raw = row.get(key, "")
    return float(raw) if raw not in {"", None} else 0.0


def norm(values: list[float], base: float | None = None) -> list[float]:
    if base is None:
        base = values[0]
    return [v / base if base else math.nan for v in values]


def polish(ax) -> None:
    ax.grid(axis="y", color="#bdbdbd", linestyle=(0, (1.5, 5.0)), linewidth=0.7)
    ax.set_axisbelow(True)
    for spine in ax.spines.values():
        spine.set_color("black")
        spine.set_linewidth(0.8)
    ax.tick_params(axis="both", width=0.8, length=4, colors="black")


def finish_two_panel(fig) -> None:
    fig.subplots_adjust(left=0.16, right=0.985, bottom=0.30, top=0.805, wspace=0.55)


def add_bar_labels(ax, bars, values, offset, fmt="{:.2f}", fontsize=10, base=None) -> None:
    tops = base if base is not None else [bar.get_height() for bar in bars]
    for bar, value, top in zip(bars, values, tops):
        ax.text(
            bar.get_x() + bar.get_width() / 2,
            top + offset,
            fmt.format(value),
            ha="center",
            va="bottom",
            fontsize=fontsize,
            color="black",
            clip_on=False,
        )


def savefig(fig, out_dir: Path, name: str, formats: list[str]) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    for fmt in formats:
        fig.savefig(out_dir / f"{name}.{fmt}", dpi=600, facecolor="white")
    plt.close(fig)


# ---------------------------------------------------------------- Figure 16
def plot_placement(rows, out_dir, formats) -> None:
    order = ["no_interleave", "leaf_4way", "global_16way"]
    labels = [LAYOUT_LABELS[x] for x in order]
    colors = [COLORS["non"], COLORS["group"], COLORS["flat"]]
    data = [row for row in rows if row["section"] == "1.1" and row["profile"] == "full" and row["layout"] in order]
    by_layout = {row["layout"]: row for row in data}
    data = [by_layout[x] for x in order]
    bw = norm([num(r, "host_sum_bw_gbps") for r in data])
    lat = norm([num(r, "host_weighted_avg_latency_ns") for r in data])

    fig, axes = plt.subplots(1, 2, figsize=FIGSIZE)
    x = np.arange(len(order))
    for ax, y, ylabel, ylim, off in [
        (axes[0], bw, "Norm. bandwidth", (0, 4.60), 0.07),
        (axes[1], lat, "Norm. latency", (0, 1.25), 0.02),
    ]:
        bars = ax.bar(x, y, color=colors, edgecolor="black", linewidth=0.8, width=0.58)
        ax.set_xticks(x, labels)
        ax.set_ylabel(ylabel)
        ax.set_ylim(*ylim)
        polish(ax)
        add_bar_labels(ax, bars, y, off)
    finish_two_panel(fig)
    savefig(fig, out_dir, "fig0609_placement_perfect", formats)


# ---------------------------------------------------------------- Figure 17
def plot_footprint(footprint_rows, out_dir, formats) -> None:
    layouts = ["no_interleave", "leaf_4way", "global_16way"]
    regimes = [
        ("single-endpoint regime", "Endpoint"),
        ("leaf-group regime", "Leaf"),
        ("beyond-leaf/global regime", "Global"),
    ]
    by_key = {(r["regime"], r["layout"]): r for r in footprint_rows}
    base_latency = num(by_key[(regimes[0][0], layouts[0])], "host_weighted_avg_latency_ns")
    x = np.arange(len(regimes))
    width = 0.24

    fig, axes = plt.subplots(1, 2, figsize=FIGSIZE)
    for offset, layout in zip([-width, 0.0, width], layouts):
        data = [by_key[(regime, layout)] for regime, _ in regimes]
        lat = norm([num(r, "host_weighted_avg_latency_ns") for r in data], base_latency)
        touched = [num(r, "sanity_mean_touched_endpoints") for r in data]
        for ax, y in ((axes[0], lat), (axes[1], touched)):
            ax.bar(
                x + offset,
                y,
                color=LAYOUT_COLORS[layout],
                edgecolor="black",
                linewidth=0.8,
                width=width,
                label=LAYOUT_LABELS[layout],
            )

    labels = [label for _, label in regimes]
    for ax, ylabel in [(axes[0], "Norm. latency"), (axes[1], "Touched EPs")]:
        ax.set_xticks(x, labels)
        ax.set_ylabel(ylabel)
        polish(ax)
    axes[0].yaxis.set_label_coords(-0.23, 0.35)
    axes[1].yaxis.set_label_coords(-0.24, 0.35)
    axes[0].set_ylim(0, 2.2)
    axes[1].set_ylim(0, 18)

    handles = [
        Patch(facecolor=LAYOUT_COLORS[x], edgecolor="black", linewidth=0.8, label=LAYOUT_LABELS[x]) for x in layouts
    ]
    fig.legend(
        handles,
        [LAYOUT_LABELS[x] for x in layouts],
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
        handler_map={Patch: square_patch_handler()},
    )
    finish_two_panel(fig)
    savefig(fig, out_dir, "fig0609_footprint_regimes", formats)


# ---------------------------------------------------------------- Figure 18
def plot_group_size(rows, out_dir, formats) -> None:
    order = ["local_2way", "leaf_4way", "crossleaf_8way"]
    labels = ["2-way", "4-way", "8-way"]
    colors = [COLORS["non"], COLORS["group"], COLORS["flat"]]
    by_layout = {
        row["layout"]: row
        for row in rows
        if row["section"] == "1.3" and row["profile"] == "full" and row["layout"] in order
    }
    data = [by_layout[x] for x in order]
    bw = norm([num(r, "host_sum_bw_gbps") for r in data])
    lat = norm([num(r, "host_weighted_avg_latency_ns") for r in data])

    fig, axes = plt.subplots(1, 2, figsize=FIGSIZE)
    x = np.arange(len(order))
    for ax, y, ylabel, ylim, off in [
        (axes[0], bw, "Norm. bandwidth", (0, 2.30), 0.045),
        (axes[1], lat, "Norm. latency", (0, 1.25), 0.025),
    ]:
        bars = ax.bar(x, y, color=colors, edgecolor="black", linewidth=0.8, width=0.58)
        ax.set_xticks(x, labels)
        ax.set_ylabel(ylabel)
        ax.set_ylim(*ylim)
        polish(ax)
        add_bar_labels(ax, bars, y, off)
    finish_two_panel(fig)
    savefig(fig, out_dir, "fig0609_group_size_sensitivity", formats)


# ---------------------------------------------------------------- Figure 21
def plot_sf_placement(regime_root: Path, out_dir, formats) -> None:
    mappings = ["non", "leaf", "global"]
    labels = [MAPPING_LABELS[m] for m in mappings]
    colors = [MAPPING_COLORS[m] for m in mappings]

    per_seed: dict[int, dict[str, dict[str, float]]] = {}
    for seed in SEEDS:
        tag = f"P64-s{seed}"
        rows = load_rows(regime_root / tag / "wl2_results.csv")
        per_seed[seed] = {
            r["mapping"]: {
                "inv": num(r, "snoop_sum_total_inv_packets"),
                "lat": num(r, "host_weighted_avg_latency_ns"),
            }
            for r in rows
        }

    def ratio_stats(metric: str):
        mean, lo, hi = [], [], []
        for m in mappings:
            ratios = [per_seed[s][m][metric] / per_seed[s]["non"][metric] for s in SEEDS]
            mean.append(sum(ratios) / len(ratios))
            lo.append(min(ratios))
            hi.append(max(ratios))
        return mean, lo, hi

    lat_mean, lat_lo, lat_hi = ratio_stats("lat")
    inv_mean, inv_lo, inv_hi = ratio_stats("inv")
    x = np.arange(len(mappings))

    fig, axes = plt.subplots(1, 2, figsize=FIGSIZE)
    for ax, mean, lo, hi, ylabel, ylim, fmt, off in [
        (axes[0], lat_mean, lat_lo, lat_hi, "Norm. latency", (0, 1.40), "{:.3f}", 0.03),
        (axes[1], inv_mean, inv_lo, inv_hi, "Norm. BISnp count", (0, 2.10), "{:.2f}", 0.03),
    ]:
        bars = ax.bar(
            x,
            mean,
            color=colors,
            edgecolor="black",
            linewidth=0.8,
            width=0.58,
            yerr=[np.array(mean) - np.array(lo), np.array(hi) - np.array(mean)],
            capsize=2.5,
            error_kw={"elinewidth": 0.8, "ecolor": "black", "capthick": 0.8},
        )
        ax.axhline(1.0, color="#7f7f7f", linestyle=(0, (1.5, 4.0)), linewidth=0.8, zorder=0)
        ax.set_xticks(x, labels)
        ax.set_ylabel(ylabel, fontsize=12)
        ax.set_ylim(*ylim)
        polish(ax)
        add_bar_labels(ax, bars, mean, off, fmt=fmt, base=hi)
    finish_two_panel(fig)
    savefig(fig, out_dir, "fig0609_sf_placement_pressure", formats)


# ---------------------------------------------------------------- Figure 22
def plot_fs_capacity(cap_csv: Path, out_dir, formats) -> None:
    rows = load_rows(cap_csv)
    ways = sorted({int(float(r["ways"])) for r in rows})
    stats = []
    for w in ways:
        group = [r for r in rows if int(float(r["ways"])) == w]
        stats.append(
            {
                "ways": w,
                "x": num(group[0], "capacity_ratio_sf_over_1025"),
                "lifo_inv": [num(r, "LIFO") for r in group],
                "fs_inv": [num(r, "FS") for r in group],
                "lifo_lat": [num(r, "lat_LIFO") for r in group],
                "fs_lat": [num(r, "lat_FS") for r in group],
            }
        )

    base_inv = stats[0]["lifo_inv"]
    base_lat = stats[0]["lifo_lat"]
    floor = 1e-3

    def rel(vals, base):
        out = []
        for v, b in zip(vals, base):
            out.append(v / b if b else math.nan)
        return out

    def series_stats(vals_per_x, clip=None):
        mean, lo, hi = [], [], []
        for column in vals_per_x:
            values = [v for v in column if not math.isnan(v)]
            if clip is not None:
                values = [max(v, clip) for v in values]
            mean.append(sum(values) / len(values))
            lo.append(min(values))
            hi.append(max(values))
        return mean, lo, hi

    xs = [s["x"] for s in stats]
    lifo_inv = [rel(s["lifo_inv"], base_inv) if s["lifo_inv"] != [0.0] * len(SEEDS) else [0.0] * len(SEEDS) for s in stats]
    fs_inv = [rel(s["fs_inv"], base_inv) if s["fs_inv"] != [0.0] * len(SEEDS) else [0.0] * len(SEEDS) for s in stats]
    lifo_lat = [rel(s["lifo_lat"], base_lat) for s in stats]
    fs_lat = [rel(s["fs_lat"], base_lat) for s in stats]

    fig, axes = plt.subplots(1, 2, figsize=FIGSIZE)
    for ax, series_a, series_b, ylabel, ylim in [
        (axes[0], lifo_inv, fs_inv, "Norm. BISnp count", (5e-4, 1.6)),
        (axes[1], lifo_lat, fs_lat, "Norm. latency", (0.85, 1.05)),
    ]:
        for vals, color, marker, label in [
            (series_a, COLORS["non"], "o", "LIFO"),
            (series_b, COLORS["flat"], "s", "FS+LIFO"),
        ]:
            mean, lo, hi = series_stats(vals, clip=floor)
            ax.errorbar(
                xs,
                mean,
                yerr=[np.array(mean) - np.array(lo), np.array(hi) - np.array(mean)],
                color=color,
                marker=marker,
                markersize=5.0,
                linewidth=1.4,
                capsize=2.0,
                elinewidth=0.8,
                markeredgecolor="black",
                markeredgewidth=0.6,
                label=label,
            )
        ax.set_ylim(*ylim)
        ax.set_ylabel(ylabel)
        ax.set_xlabel("Capacity / hot footprint")
        polish(ax)
    axes[0].set_yscale("log")
    for ax in axes:
        ax.yaxis.label.set_size(12)
    axes[0].yaxis.set_major_locator(mpl.ticker.LogLocator(base=10))
    axes[0].yaxis.set_major_formatter(mpl.ticker.FuncFormatter(lambda y, _: f"{y:g}"))
    axes[0].yaxis.set_minor_formatter(mpl.ticker.NullFormatter())

    # annotate the FS/LIFO ratio at ways=4 and the zero at ways=8
    w4 = next(s for s in stats if s["ways"] == 4)
    ratios4 = [fs / lifo for fs, lifo in zip(w4["fs_inv"], w4["lifo_inv"]) if lifo]
    axes[0].annotate(
        f"FS/LIFO = {sum(ratios4) / len(ratios4):.2f}",
        xy=(w4["x"], max(floor, min(1.0, sum(rel(w4["fs_inv"], base_inv)) / len(SEEDS)))),
        xytext=(w4["x"] + 0.03, 0.06),
        fontsize=9,
        arrowprops={"arrowstyle": "-", "linewidth": 0.6, "color": "black"},
    )
    zero = stats[-1]
    axes[0].annotate(
        "0",
        xy=(zero["x"], floor),
        xytext=(zero["x"] - 0.12, floor * 1.6),
        fontsize=9,
    )
    handles = [
        mpl.lines.Line2D([], [], color=COLORS["non"], marker="o", linewidth=1.4, markeredgecolor="black", label="LIFO"),
        mpl.lines.Line2D([], [], color=COLORS["flat"], marker="s", linewidth=1.4, markeredgecolor="black", label="FS+LIFO"),
    ]
    fig.legend(
        handles,
        ["LIFO", "FS+LIFO"],
        loc="upper left",
        bbox_to_anchor=(0.18, 0.985),
        ncol=2,
        frameon=False,
        handlelength=1.4,
        handletextpad=0.4,
        columnspacing=0.9,
        borderaxespad=0.0,
    )
    finish_two_panel(fig)
    savefig(fig, out_dir, "fig0609_fs_capacity_window", formats)


# ---------------------------------------------------------------- Figure 23
def plot_fs_sharing(ratio_csv: Path, p64_root: Path, out_dir, formats) -> None:
    rows = load_rows(ratio_csv)
    ratios = sorted({float(r["ratio"]) for r in rows})
    inv = {r: [] for r in ratios}
    lat = {r: [] for r in ratios}
    for r in rows:
        key = float(r["ratio"])
        seed = int(float(r["seed"]))
        inv[key].append(num(r, "P64_FS") / num(r, "P64_LIFO"))
        tag_rows = load_rows(p64_root / f"r{key}_s{seed}" / "wl2_results.csv")
        by_policy = {x["policy"]: num(x, "host_weighted_avg_latency_ns") for x in tag_rows}
        lat[key].append(by_policy["FS+LIFO"] / by_policy["LIFO"])

    def mean_lo_hi(vals):
        return sum(vals) / len(vals), min(vals), max(vals)

    fig, axes = plt.subplots(1, 2, figsize=FIGSIZE)
    x = np.arange(len(ratios))
    for ax, table, ylabel in [
        (axes[0], inv, "Norm. BISnp count"),
        (axes[1], lat, "Norm. latency"),
    ]:
        mean, lo, hi = zip(*(mean_lo_hi(table[r]) for r in ratios))
        ax.errorbar(
            x,
            mean,
            yerr=[np.array(mean) - np.array(lo), np.array(hi) - np.array(mean)],
            color=COLORS["flat"],
            marker="s",
            markersize=5.0,
            linewidth=1.4,
            capsize=2.0,
            elinewidth=0.8,
            markeredgecolor="black",
            markeredgewidth=0.6,
        )
        ax.axhline(1.0, color="#7f7f7f", linestyle=(0, (1.5, 4.0)), linewidth=0.8, zorder=0)
        ax.set_xlabel("Shared data (%)")
        ax.set_xticks(x, [f"{int(r * 100)}" for r in ratios])
        ax.set_xlim(-0.45, len(ratios) - 0.55)
        ax.set_ylim(0.40, 1.10)
        ax.set_ylabel(ylabel, fontsize=12)
        polish(ax)
        if table is inv:
            for xi, value, top in zip(x, mean, hi):
                ax.text(xi, top + 0.02, f"{value:.2f}", ha="center", va="bottom", fontsize=10, clip_on=False)
    finish_two_panel(fig)
    savefig(fig, out_dir, "fig0609_fs_sharing_shape", formats)


def check_fonts(out_dir: Path) -> None:
    checked = 0
    for name in FIGURE_NAMES:
        path = out_dir / f"{name}.eps"
        if not path.is_file():
            continue
        text = path.read_text(encoding="latin-1")
        assert "/Helvetica" in text, f"{name}: no Helvetica"
        assert "/Arial" not in text, f"{name}: contains Arial"
        assert "/FontType 42" not in text, f"{name}: contains Type 42"
        checked += 1
    print(f"font check: {checked}/{len(FIGURE_NAMES)} present, Helvetica/AFM, no Arial, no Type 42")




# ------------------------------------------------- Section 5.4 with ClnEvct on/off
def load_wl2_root(root: Path) -> dict[str, list[dict[str, str]]]:
    """Return {tag: rows} for every tag directory that has wl2_results.csv."""
    out: dict[str, list[dict[str, str]]] = {}
    if not root.exists():
        return out
    for d in sorted(root.iterdir()):
        f = d / "wl2_results.csv"
        if f.is_file():
            out[d.name] = load_rows(f)
    return out


def _wl2_family(tags: dict[str, list[dict[str, str]]], prefix: str) -> dict[str, list[dict[str, str]]]:
    return {t: rows for t, rows in tags.items() if t.startswith(prefix)}


def _stats(values: list[float]) -> tuple[float, float, float]:
    return sum(values) / len(values), min(values), max(values)


def plot_sf_placement_onoff(on_root: Path, off_root: Path, out_dir, formats) -> None:
    mappings = ["non", "leaf", "global"]
    labels = [MAPPING_LABELS[m] for m in mappings]

    def gather(root: Path) -> dict[str, dict[str, list[float]]]:
        tags = _wl2_family(load_wl2_root(root), "P64-s")
        acc: dict[str, dict[str, list[float]]] = {m: {"inv": [], "lat": []} for m in mappings}
        for rows in tags.values():
            for r in rows:
                if r.get("policy") != "LIFO" or r.get("mapping") not in acc:
                    continue
                acc[r["mapping"]]["inv"].append(num(r, "snoop_sum_total_inv_packets"))
                acc[r["mapping"]]["lat"].append(num(r, "host_weighted_avg_latency_ns"))
        return acc

    on = gather(on_root)
    off = gather(off_root)
    has_off = all(off[m]["inv"] for m in mappings)

    fig, axes = plt.subplots(1, 2, figsize=FIGSIZE)
    x = np.arange(len(mappings))
    width = 0.36
    floor = 100.0
    for ax, key, ylabel, ylim in [
        (axes[0], "lat", "Latency (ns)", (0, 24000)),
        (axes[1], "inv", "BISnp count", (100, 20000)),
    ]:
        bottom = floor if key == "inv" else 0.0
        if has_off:
            vals = [_stats(off[m][key])[0] - bottom for m in mappings]
            err_lo = [_stats(off[m][key])[0] - _stats(off[m][key])[1] for m in mappings]
            err_hi = [_stats(off[m][key])[2] - _stats(off[m][key])[0] for m in mappings]
            ax.bar(
                x - width / 2,
                vals,
                width,
                bottom=bottom,
                color=COLORS["non"],
                edgecolor="black",
                linewidth=0.8,
                yerr=[err_lo, err_hi],
                capsize=2.5,
                error_kw={"elinewidth": 0.8, "ecolor": "black", "capthick": 0.8},
            )
        vals = [_stats(on[m][key])[0] - bottom for m in mappings]
        err_lo = [max(_stats(on[m][key])[0] - bottom - (_stats(on[m][key])[1] - bottom), 0.0) for m in mappings]
        err_hi = [max(_stats(on[m][key])[2] - _stats(on[m][key])[0], 0.0) for m in mappings]
        ax.bar(
            x + (width / 2 if has_off else 0.0),
            vals,
            width,
            bottom=bottom,
            color=[MAPPING_COLORS[m] for m in mappings],
            edgecolor="black",
            linewidth=0.8,
            yerr=[err_lo, err_hi],
            capsize=2.5,
            error_kw={"elinewidth": 0.8, "ecolor": "black", "capthick": 0.8},
        )
        ax.set_xticks(x, labels)
        ax.set_ylabel(ylabel, fontsize=11)
        ax.set_ylim(*ylim)
        ax.tick_params(axis="x", labelsize=10)
        polish(ax)
        if key == "inv":
            ax.set_yscale("log")

    handles = [
        Patch(facecolor=COLORS["non"], edgecolor="black", linewidth=0.8, label="off"),
        Patch(facecolor=COLORS["flat"], edgecolor="black", linewidth=0.8, label="on"),
    ]
    fig.legend(
        handles,
        ["ClnEvct off", "ClnEvct on"],
        loc="upper center",
        bbox_to_anchor=(0.55, 1.00),
        ncol=2,
        frameon=False,
        handlelength=1.0,
        handleheight=1.0,
        handletextpad=0.35,
        columnspacing=0.8,
        borderaxespad=0.0,
        handler_map={Patch: square_patch_handler()},
    )
    finish_two_panel(fig)
    savefig(fig, out_dir, "fig0609_sf_placement_pressure", formats)


def plot_fs_capacity_onoff(on_root: Path, off_root: Path, out_dir, formats) -> None:
    def gather(root: Path, notify_key: str) -> dict[int, dict[str, list[float]]]:
        tags = _wl2_family(load_wl2_root(root), "cap_w")
        acc: dict[int, dict[str, list[float]]] = {}
        for rows in tags.values():
            for r in rows:
                if r.get("clean_evict_notify") != notify_key:
                    continue
                ways = int(float(r["sf_assoc"]))
                slot = acc.setdefault(ways, {"lifo_inv": [], "fs_inv": [], "lifo_lat": [], "fs_lat": []})
                policy = r.get("policy")
                if policy == "LIFO":
                    slot["lifo_inv"].append(num(r, "snoop_sum_total_inv_packets"))
                    slot["lifo_lat"].append(num(r, "host_weighted_avg_latency_ns"))
                elif policy == "FS+LIFO":
                    slot["fs_inv"].append(num(r, "snoop_sum_total_inv_packets"))
                    slot["fs_lat"].append(num(r, "host_weighted_avg_latency_ns"))
        return acc

    on = gather(on_root, "true")
    off = gather(off_root, "false")
    if not on:
        return
    ways_on = sorted(on)
    xs = [w * 129 / 1025 for w in ways_on]
    floor = 40.0

    def series(acc, ways, key, clip=None):
        means, los, his = [], [], []
        for w in ways:
            if w not in acc or not acc[w][key]:
                means.append(math.nan)
                los.append(math.nan)
                his.append(math.nan)
                continue
            mean, lo, hi = _stats(acc[w][key])
            if clip is not None:
                mean, lo, hi = max(mean, clip), max(lo, clip), max(hi, clip)
            means.append(mean)
            los.append(lo)
            his.append(hi)
        return means, los, his

    fig, axes = plt.subplots(1, 2, figsize=FIGSIZE)
    styles = []
    if off:
        ways_off = [w for w in ways_on if w in off]
        styles.append(("off", off, ways_off, [w * 129 / 1025 for w in ways_off], "--", "o"))
    styles.append(("on", on, ways_on, xs, "-", "s"))

    for ax, key, ylabel, ylim in [
        (axes[0], "inv", "BISnp count", (floor, 70000)),
        (axes[1], "lat", "Latency (ns)", (18500, 21000)),
    ]:
        for _state, acc, ways_state, xs_state, ls, marker in styles:
            for _policy, pkey, color, fill in [
                ("LIFO", "lifo_" + key, COLORS["non"], False),
                ("FS+LIFO", "fs_" + key, COLORS["flat"], True),
            ]:
                clip = floor if key == "inv" else None
                means, los, his = series(acc, ways_state, pkey, clip=clip)
                ax.errorbar(
                    xs_state,
                    means,
                    yerr=[[max(m - l, 0.0) for m, l in zip(means, los)], [max(h - m, 0.0) for m, h in zip(means, his)]],
                    color=color,
                    marker=marker,
                    markersize=4.5,
                    linewidth=1.2,
                    linestyle=ls,
                    markerfacecolor=color if fill else "white",
                    markeredgecolor="black",
                    markeredgewidth=0.6,
                    capsize=1.8,
                    elinewidth=0.7,
                )
        ax.set_ylim(*ylim)
        ax.set_ylabel(ylabel, fontsize=11)
        ax.set_xlabel("Capacity / hot footprint", fontsize=10.5)
        ax.tick_params(axis="x", labelsize=9.5)
        polish(ax)
    axes[0].set_yscale("log")
    axes[0].yaxis.set_major_locator(mpl.ticker.LogLocator(base=10))
    axes[0].yaxis.set_major_formatter(mpl.ticker.FuncFormatter(lambda v, _: f"{v:g}"))
    axes[0].yaxis.set_minor_formatter(mpl.ticker.NullFormatter())
    last_x = xs[-1]
    axes[0].annotate("0 (on)", xy=(last_x, floor), xytext=(last_x - 0.16, floor * 1.6), fontsize=8.5)
    handles = [
        mpl.lines.Line2D([], [], color=COLORS["non"], marker="o", linewidth=1.4, linestyle="-", markeredgecolor="black", label="LIFO"),
        mpl.lines.Line2D([], [], color=COLORS["flat"], marker="s", linewidth=1.4, linestyle="-", markeredgecolor="black", label="FS+LIFO"),
    ]
    fig.legend(
        handles,
        ["LIFO", "FS+LIFO"],
        loc="upper center",
        bbox_to_anchor=(0.55, 1.00),
        ncol=2,
        frameon=False,
        handlelength=1.6,
        handletextpad=0.4,
        columnspacing=0.9,
        borderaxespad=0.0,
    )
    finish_two_panel(fig)
    fig.subplots_adjust(top=0.75)
    savefig(fig, out_dir, "fig0609_fs_capacity_window", formats)


def plot_fs_sharing_onoff(on_root: Path, off_root: Path, out_dir, formats) -> None:
    def gather(root: Path, notify_key: str) -> dict[float, list[tuple[float, float]]]:
        tags = _wl2_family(load_wl2_root(root), "r")
        acc: dict[float, list[tuple[float, float]]] = {}
        for rows in tags.values():
            if not any(r.get("shared_ratio") for r in rows):
                continue
            for r in rows:
                if r.get("clean_evict_notify") != notify_key:
                    continue
                key = float(r["shared_ratio"])
                if key not in (0.25, 0.5, 0.75):
                    continue
                slot = acc.setdefault(key, [])
                slot.append(r)
        # reduce pairs of (LIFO, FS) rows per tag into ratios
        out: dict[float, list[tuple[float, float]]] = {}
        for key, rows in acc.items():
            out[key] = []
            # rows are grouped by tag order; pair sequentially
            buffer: dict[str, dict[str, float]] = {}
            for r in rows:
                tag = r.get("tag", "") + "@" + str(r.get("seed_base"))
                buffer.setdefault(tag, {})[r["policy"]] = r
            for tag, pair in buffer.items():
                if "LIFO" in pair and "FS+LIFO" in pair:
                    lifo, fs = pair["LIFO"], pair["FS+LIFO"]
                    inv = num(fs, "snoop_sum_total_inv_packets") / num(lifo, "snoop_sum_total_inv_packets")
                    lat = num(fs, "host_weighted_avg_latency_ns") / num(lifo, "host_weighted_avg_latency_ns")
                    out[key].append((inv, lat))
        return out

    on = gather(on_root, "true")
    off = gather(off_root, "false")
    if not on:
        return
    ratios = sorted(on)
    x = np.arange(len(ratios))

    fig, axes = plt.subplots(1, 2, figsize=FIGSIZE)
    for ax, idx, ylabel in [(axes[0], 0, "BISnp ratio"), (axes[1], 1, "Latency ratio")]:
        for table, color, marker, ls, label in [
            (off, COLORS["non"], "o", "--", "ClnEvct off"),
            (on, COLORS["flat"], "s", "-", "ClnEvct on"),
        ]:
            if not table:
                continue
            means, los, his = [], [], []
            for r in ratios:
                vals = [row[idx] for row in table.get(r, [])]
                if not vals:
                    means.append(math.nan)
                    los.append(math.nan)
                    his.append(math.nan)
                    continue
                m, lo, hi = _stats(vals)
                means.append(m)
                los.append(lo)
                his.append(hi)
            ax.errorbar(
                x,
                means,
                yerr=[[m - l for m, l in zip(means, los)], [h - m for m, h in zip(means, his)]],
                color=color,
                marker=marker,
                markersize=4.5,
                linewidth=1.4,
                linestyle=ls,
                markeredgecolor="black",
                markeredgewidth=0.6,
                capsize=2.0,
                elinewidth=0.8,
                label=label,
            )
        ax.axhline(1.0, color="#7f7f7f", linestyle=(0, (1.5, 4.0)), linewidth=0.8, zorder=0)
        ax.set_xlabel("Shared data (%)", fontsize=11)
        ax.set_xticks(x, [f"{int(r * 100)}" for r in ratios])
        ax.set_xlim(-0.45, len(ratios) - 0.55)
        ax.set_ylim(*(0.40, 1.10) if ylabel.startswith("BISnp") else (0.95, 1.05))
        ax.set_ylabel(ylabel, fontsize=11)
        ax.tick_params(axis="x", labelsize=10)
        polish(ax)
    handles, labels = axes[1].get_legend_handles_labels()
    fig.legend(
        handles,
        labels,
        loc="upper center",
        bbox_to_anchor=(0.55, 1.00),
        ncol=2,
        frameon=False,
        handlelength=1.6,
        handletextpad=0.35,
        columnspacing=0.9,
        borderaxespad=0.0,
        prop={"size": 10.5},
    )
    finish_two_panel(fig)
    fig.subplots_adjust(top=0.75)
    savefig(fig, out_dir, "fig0609_fs_sharing_shape", formats)




def plot_fs_sharing_scopes(global_roots: list[Path], leaf_root: Path, out_dir, formats) -> None:
    """FS/LIFO ratio vs shared-data ratio, for Global and Leaf sharing scopes, on/off."""

    def gather(roots: list[Path], notify_key: str, prefix: str) -> dict[float, list[tuple[float, float]]]:
        acc: dict[float, list[tuple[float, float]]] = {}
        for root in roots:
            for tag, rows in _wl2_family(load_wl2_root(root), prefix).items():
                pairs: dict[str, dict[str, dict[str, str]]] = {}
                for r in rows:
                    if r.get("clean_evict_notify") != notify_key:
                        continue
                    try:
                        ratio = float(r["shared_ratio"])
                    except (TypeError, ValueError):
                        continue
                    if ratio not in (0.25, 0.5, 0.75):
                        continue
                    key = str(r.get("seed_base"))
                    pairs.setdefault(key, {})[r["policy"]] = r
                for _seed, pair in pairs.items():
                    if "LIFO" not in pair or "FS+LIFO" not in pair:
                        continue
                    lifo, fs = pair["LIFO"], pair["FS+LIFO"]
                    key = float(fs["shared_ratio"])
                    inv = num(fs, "snoop_sum_total_inv_packets") / num(lifo, "snoop_sum_total_inv_packets")
                    lat = num(fs, "host_weighted_avg_latency_ns") / num(lifo, "host_weighted_avg_latency_ns")
                    acc.setdefault(key, []).append((inv, lat))
        return acc

    global_on = gather(list(global_roots), "true", "r")
    global_off = gather(list(global_roots), "false", "r")
    leaf_on = gather([leaf_root], "true", "leaf_r")
    leaf_off = gather([leaf_root], "false", "leaf_r")
    base = global_on or global_off or leaf_on or leaf_off
    if not base:
        return
    ratios = sorted(base)
    x = np.arange(len(ratios))

    series = [
        ("Global, off", global_off, COLORS["non"], "o", "--"),
        ("Global, on", global_on, COLORS["flat"], "s", "-"),
        ("Leaf, off", leaf_off, COLORS["non"], "^", ":"),
        ("Leaf, on", leaf_on, COLORS["group"], "^", "-"),
    ]

    fig, axes = plt.subplots(1, 2, figsize=FIGSIZE)
    for ax, idx, ylabel in [(axes[0], 0, "BISnp ratio"), (axes[1], 1, "Latency ratio")]:
        for _label, table, color, marker, ls in series:
            if not table:
                continue
            means, los, his = [], [], []
            for r in ratios:
                vals = [row[idx] for row in table.get(r, [])]
                if not vals:
                    means.append(math.nan)
                    los.append(math.nan)
                    his.append(math.nan)
                    continue
                mean, lo, hi = _stats(vals)
                means.append(mean)
                los.append(lo)
                his.append(hi)
            ax.errorbar(
                x,
                means,
                yerr=[[max(m - l, 0.0) for m, l in zip(means, los)], [max(h - m, 0.0) for m, h in zip(means, his)]],
                color=color,
                marker=marker,
                markersize=4.5,
                linewidth=1.3,
                linestyle=ls,
                markeredgecolor="black",
                markeredgewidth=0.6,
                capsize=1.8,
                elinewidth=0.7,
            )
        ax.axhline(1.0, color="#7f7f7f", linestyle=(0, (1.5, 4.0)), linewidth=0.8, zorder=0)
        ax.set_xlabel("Shared data (%)", fontsize=11)
        ax.set_xticks(x, [f"{int(r * 100)}" for r in ratios])
        ax.set_xlim(-0.45, len(ratios) - 0.55)
        ax.set_ylim(*(0.40, 1.10) if ylabel.startswith("BISnp") else (0.95, 1.05))
        ax.set_ylabel(ylabel, fontsize=11)
        ax.tick_params(axis="x", labelsize=10)
        polish(ax)

    handles = [
        mpl.lines.Line2D([], [], color=color, marker=marker, linestyle=ls, linewidth=1.3, markeredgecolor="black", label=label)
        for label, _t, color, marker, ls in series
    ]
    fig.legend(
        handles,
        [label for label, *_ in series],
        loc="upper center",
        bbox_to_anchor=(0.55, 1.00),
        ncol=2,
        frameon=False,
        handlelength=1.5,
        handletextpad=0.35,
        columnspacing=0.8,
        borderaxespad=0.0,
        prop={"size": 9.5},
    )
    finish_two_panel(fig)
    fig.subplots_adjust(top=0.74)
    savefig(fig, out_dir, "fig0609_fs_sharing_shape", formats)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ext-csv", type=Path, default=DEFAULT_EXT)
    parser.add_argument("--footprint-csv", type=Path, default=DEFAULT_FOOTPRINT)
    parser.add_argument("--regime-root", type=Path, default=DEFAULT_REGIME)
    parser.add_argument("--regime-off-root", type=Path, default=DEFAULT_REGIME_OFF)
    parser.add_argument("--p64-off-root", type=Path, default=DEFAULT_P64_OFF)
    parser.add_argument("--p64-leaf-root", type=Path, default=DEFAULT_P64_LEAF)
    parser.add_argument("--cap-csv", type=Path, default=DEFAULT_CAP)
    parser.add_argument("--ratio-csv", type=Path, default=DEFAULT_RATIO)
    parser.add_argument("--p64-root", type=Path, default=DEFAULT_P64)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--formats", nargs="+", choices=("jpg", "eps", "pdf"), default=["jpg", "eps"])
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--only", default="placement,footprint,group_size",
                        help="comma-separated: placement,footprint,group_size")
    args = parser.parse_args()
    if args.list:
        print("\n".join(FIGURE_NAMES))
        return 0

    import_plotting()
    setup_style()
    ext = load_rows(args.ext_csv)
    wanted = {x.strip() for x in args.only.split(",") if x.strip()}
    if "placement" in wanted:
        plot_placement(ext, args.out_dir, args.formats)
    if "footprint" in wanted:
        plot_footprint(load_rows(args.footprint_csv), args.out_dir, args.formats)
    if "group_size" in wanted:
        plot_group_size(ext, args.out_dir, args.formats)
    # Section 5.4 is produced by plot_s54_bus8.py from the d=8 dataset; the old
    # zero-delay roots (_p64/_p64_off/_p64_leaf) were removed on 2026-10-31.
    if "eps" in args.formats:
        check_fonts(args.out_dir)
    print(f"wrote figures to {args.out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
