#!/usr/bin/env python3
"""Generate the six AE extension figures from summarized experiment CSVs."""

from __future__ import annotations

import argparse
import csv
import logging
import math
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CSV = ROOT / "results" / "ae_extension" / "ae_extension_results.csv"
DEFAULT_FOOTPRINT_CSV = ROOT / "results" / "ae_footprint_regimes" / "ae_footprint_plot_points.csv"
DEFAULT_OUT_DIR = ROOT / "output" / "ae_figures"
FIGURE_NAMES = (
    "fig_ae_mapping_performance",
    "fig_ae_mapping_footprint",
    "fig_ae_mapping_group_size",
    "fig_ae_sf_mapping_pressure",
    "fig_ae_fs_capacity_window",
    "fig_ae_fs_sharing_scope",
)

LAYOUT_LABELS = {
    "no_interleave": "Non",
    "leaf_4way": "Group",
    "global_16way": "Flat",
    "local_2way": "2-way",
    "crossleaf_8way": "8-way",
}
COLORS = {
    "non": "#C0C0C0",
    "group": "#00C8C8",
    "flat": "#00A0FF",
    "teal": "#00C8C8",
    "blue": "#00A0FF",
    "gray": "#C0C0C0",
}
FIGSIZE = (5.25, 1.80)
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

    mpl = mpl_module
    plt = plt_module
    np = np_module
    Patch = PatchClass
    Rectangle = RectangleClass


def square_patch_handler():
    import_plotting()
    from matplotlib.legend_handler import HandlerPatch

    class SquarePatchHandler(HandlerPatch):
        def create_artists(self, legend, orig_handle, xdescent, ydescent, width, height, fontsize, trans):
            side = height * 0.80
            y = ydescent + (height - side) / 2
            patch = Rectangle(
                (xdescent, y),
                side,
                side,
                facecolor=orig_handle.get_facecolor(),
                edgecolor=orig_handle.get_edgecolor(),
                linewidth=orig_handle.get_linewidth(),
                transform=trans,
            )
            return [patch]

    return SquarePatchHandler()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--csv", type=Path, default=DEFAULT_CSV, help="AE extension summary CSV.")
    parser.add_argument(
        "--footprint-csv",
        type=Path,
        default=DEFAULT_FOOTPRINT_CSV,
        help="Focused Section 1.2 footprint-regime CSV.",
    )
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    parser.add_argument("--profile", default="full")
    parser.add_argument("--formats", nargs="+", choices=("jpg", "eps", "pdf"), default=["jpg", "eps"])
    parser.add_argument("--list", action="store_true", help="List figure names and exit.")
    return parser.parse_args()


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


def load_rows(csv_path: Path) -> list[dict[str, str]]:
    if not csv_path.exists():
        raise SystemExit(f"missing CSV: {csv_path}")
    with csv_path.open(newline="") as fh:
        return list(csv.DictReader(fh))


def first_existing(primary: Path, fallback_glob: str) -> Path:
    if primary.exists():
        return primary
    matches = sorted((ROOT / "results").glob(fallback_glob))
    return matches[0] if matches else primary


def val(row: dict[str, str], key: str) -> float:
    raw = row.get(key, "")
    return float(raw) if raw not in {"", None} else 0.0


def row(rows: list[dict[str, str]], profile: str, section: str, **conds: object) -> dict[str, str]:
    matches = []
    for item in rows:
        if item["profile"] != profile or item["section"] != section:
            continue
        if all(str(item.get(key, "")) == str(expected) for key, expected in conds.items()):
            matches.append(item)
    if len(matches) != 1:
        raise ValueError(f"expected one row for profile={profile!r}, section={section!r}, {conds=}; got {len(matches)}")
    return matches[0]


def weighted_bucket(row: dict[str, str], prefix: str) -> float:
    numer = 0.0
    denom = 0.0
    for key, raw in row.items():
        if not key.startswith(prefix) or raw == "":
            continue
        try:
            bucket = int(key.removeprefix(prefix))
            count = float(raw)
        except ValueError:
            continue
        numer += bucket * count
        denom += count
    return numer / denom if denom else 0.0


def norm(values: list[float], base: float | None = None) -> list[float]:
    if base is None:
        base = values[0]
    return [value / base if base else math.nan for value in values]


def ratio(new: float, base: float) -> float:
    return new / base if base else math.nan


def polish(ax: plt.Axes) -> None:
    ax.grid(axis="y", color="#bdbdbd", linestyle=(0, (1.5, 5.0)), linewidth=0.7)
    ax.set_axisbelow(True)
    for spine in ax.spines.values():
        spine.set_color("black")
        spine.set_linewidth(0.8)
    ax.tick_params(axis="both", width=0.8, length=4, colors="black")


def finish_two_panel(fig: plt.Figure) -> None:
    fig.subplots_adjust(left=0.16, right=0.985, bottom=0.30, top=0.805, wspace=0.55)


def format_log_axis(ax: plt.Axes) -> None:
    ax.yaxis.set_major_locator(mpl.ticker.LogLocator(base=10))
    ax.yaxis.set_major_formatter(mpl.ticker.FuncFormatter(lambda y, _: f"{y:g}"))
    ax.yaxis.set_minor_formatter(mpl.ticker.NullFormatter())


def add_bar_labels(ax: plt.Axes, bars, values: list[float], offset: float) -> None:
    for bar, value in zip(bars, values):
        ax.text(
            bar.get_x() + bar.get_width() / 2,
            bar.get_height() + offset,
            f"{value:.2f}",
            ha="center",
            va="bottom",
            fontsize=10,
            color="black",
            clip_on=False,
        )


def savefig(fig: plt.Figure, out_dir: Path, name: str, formats: list[str]) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    for fmt in formats:
        fig.savefig(out_dir / f"{name}.{fmt}", dpi=600, facecolor="white")
    plt.close(fig)


def plot_placement_perfect(rows: list[dict[str, str]], profile: str, out_dir: Path, formats: list[str]) -> None:
    order = ["no_interleave", "leaf_4way", "global_16way"]
    labels = [LAYOUT_LABELS[item] for item in order]
    colors = [COLORS["non"], COLORS["group"], COLORS["flat"]]
    data = [row(rows, profile, "1.1", layout=layout) for layout in order]
    bw = norm([val(item, "host_sum_bw_gbps") for item in data])
    lat = norm([val(item, "host_weighted_avg_latency_ns") for item in data])

    fig, axes = plt.subplots(1, 2, figsize=FIGSIZE)
    x = np.arange(len(order))
    for ax, y, ylabel, ylim in [
        (axes[0], bw, "Norm. bandwidth", (0, 4.4)),
        (axes[1], lat, "Norm. latency", (0, 1.15)),
    ]:
        ax.bar(x, y, color=colors, edgecolor="black", linewidth=0.8, width=0.58)
        ax.set_xticks(x, labels)
        ax.set_ylabel(ylabel)
        ax.set_ylim(*ylim)
        polish(ax)
    finish_two_panel(fig)
    savefig(fig, out_dir, "fig_ae_mapping_performance", formats)


def plot_footprint_regimes(footprint_rows: list[dict[str, str]], out_dir: Path, formats: list[str]) -> None:
    layouts = ["no_interleave", "leaf_4way", "global_16way"]
    regimes = [
        ("single-endpoint regime", "Node"),
        ("leaf-group regime", "Group"),
        ("beyond-leaf/global regime", "Global"),
    ]
    colors = {
        "no_interleave": COLORS["non"],
        "leaf_4way": COLORS["group"],
        "global_16way": COLORS["flat"],
    }
    by_key = {(item["regime"], item["layout"]): item for item in footprint_rows}
    base_latency = val(by_key[(regimes[0][0], layouts[0])], "host_weighted_avg_latency_ns")

    x = np.arange(len(regimes))
    width = 0.24
    fig, axes = plt.subplots(1, 2, figsize=FIGSIZE)
    for offset, layout in zip([-width, 0.0, width], layouts):
        data = [by_key[(regime, layout)] for regime, _ in regimes]
        lat = norm([val(item, "host_weighted_avg_latency_ns") for item in data], base_latency)
        touched = [val(item, "sanity_mean_touched_endpoints") for item in data]
        axes[0].bar(x + offset, lat, color=colors[layout], edgecolor="black", linewidth=0.8, width=width, label=LAYOUT_LABELS[layout])
        axes[1].bar(x + offset, touched, color=colors[layout], edgecolor="black", linewidth=0.8, width=width)

    labels = [label for _, label in regimes]
    for ax, ylabel in [(axes[0], "Norm. latency"), (axes[1], "Touched EPs")]:
        ax.set_xticks(x, labels)
        ax.set_ylabel(ylabel)
        polish(ax)
    axes[0].yaxis.set_label_coords(-0.23, 0.35)
    axes[1].yaxis.set_label_coords(-0.24, 0.35)
    axes[0].set_ylim(0, 2.2)
    axes[1].set_ylim(0, 18)

    handles = [Patch(facecolor=colors[layout], edgecolor="black", linewidth=0.8, label=LAYOUT_LABELS[layout]) for layout in layouts]
    fig.legend(
        handles,
        [LAYOUT_LABELS[layout] for layout in layouts],
        frameon=False,
        facecolor="none",
        edgecolor="none",
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
    savefig(fig, out_dir, "fig_ae_mapping_footprint", formats)


def plot_group_size(rows: list[dict[str, str]], profile: str, out_dir: Path, formats: list[str]) -> None:
    order = ["local_2way", "leaf_4way", "crossleaf_8way"]
    labels = ["2-way", "4-way", "8-way"]
    colors = [COLORS["non"], COLORS["group"], COLORS["flat"]]
    data = [row(rows, profile, "1.3", layout=layout) for layout in order]
    bw = norm([val(item, "host_sum_bw_gbps") for item in data])
    lat = norm([val(item, "host_weighted_avg_latency_ns") for item in data])

    fig, axes = plt.subplots(1, 2, figsize=FIGSIZE)
    x = np.arange(len(order))
    for ax, y, ylabel, ylim, label_offset in [
        (axes[0], bw, "Norm. bandwidth", (0, 2.25), 0.055),
        (axes[1], lat, "Norm. latency", (0, 1.40), 0.035),
    ]:
        bars = ax.bar(x, y, color=colors, edgecolor="black", linewidth=0.8, width=0.58)
        ax.set_xticks(x, labels)
        ax.set_ylabel(ylabel)
        ax.set_ylim(*ylim)
        polish(ax)
        add_bar_labels(ax, bars, y, label_offset)
    finish_two_panel(fig)
    savefig(fig, out_dir, "fig_ae_mapping_group_size", formats)


def plot_sf_placement(rows: list[dict[str, str]], profile: str, out_dir: Path, formats: list[str]) -> None:
    order = ["no_interleave", "leaf_4way", "global_16way"]
    labels = [LAYOUT_LABELS[item] for item in order]
    colors = [COLORS["non"], COLORS["group"], COLORS["flat"]]
    data = [row(rows, profile, "2.1", layout=layout, policy="LIFO") for layout in order]
    lat = norm([val(item, "host_weighted_avg_latency_ns") for item in data])
    inv = norm([val(item, "snoop_sum_total_inv_packets") for item in data])
    x = np.arange(len(order))

    fig, axes = plt.subplots(1, 2, figsize=FIGSIZE)
    for ax, y, ylabel in [(axes[0], lat, "Norm. latency"), (axes[1], inv, "Norm. BISnp count")]:
        ax.bar(x, y, color=colors, edgecolor="black", linewidth=0.8, width=0.58)
        ax.set_yscale("log")
        ax.set_xticks(x, labels)
        ax.set_ylabel(ylabel)
        polish(ax)
        format_log_axis(ax)
    axes[0].set_ylim(0.8, 500)
    axes[1].set_ylim(0.8, 500)
    finish_two_panel(fig)
    savefig(fig, out_dir, "fig_ae_sf_mapping_pressure", formats)


def plot_fs_capacity(rows: list[dict[str, str]], profile: str, out_dir: Path, formats: list[str]) -> None:
    caps = [64, 96, 128, 160]
    cap_labels = ["1/4", "3/8", "1/2", "5/8"]
    lifo_inv: list[float] = []
    fs_inv: list[float] = []
    lifo_lat: list[float] = []
    fs_lat: list[float] = []
    for cap in caps:
        lifo = row(rows, profile, "2.2", snoop_entries=cap, policy="LIFO")
        fs = row(rows, profile, "2.2", snoop_entries=cap, policy="FS+LIFO")
        lifo_inv.append(val(lifo, "snoop_sum_total_inv_packets"))
        fs_inv.append(val(fs, "snoop_sum_total_inv_packets"))
        lifo_lat.append(val(lifo, "host_weighted_avg_latency_ns"))
        fs_lat.append(val(fs, "host_weighted_avg_latency_ns"))
        weighted_bucket(lifo, "snoop_sum_evict_sharers_")
        weighted_bucket(fs, "snoop_sum_evict_sharers_")

    fig, axes = plt.subplots(1, 2, figsize=FIGSIZE)
    x = np.arange(len(caps))
    series = [
        (norm(lifo_inv, lifo_inv[0]), norm(fs_inv, lifo_inv[0]), "Norm. BISnp count", (0.50, 1.15)),
        (norm(lifo_lat, lifo_lat[0]), norm(fs_lat, lifo_lat[0]), "Norm. latency", (0.35, 1.10)),
    ]
    for ax, (lifo_y, fs_y, ylabel, ylim) in zip(axes, series):
        ax.plot(x, lifo_y, color=COLORS["gray"], marker="o", markersize=5.5, linewidth=1.8, label="LIFO")
        ax.plot(x, fs_y, color=COLORS["blue"], marker="s", markersize=5.5, linewidth=1.8, label="FS+LIFO")
        ax.set_ylim(*ylim)
        ax.set_xticks(x, cap_labels)
        ax.set_xlabel("Capacity / hot footprint")
        ax.set_ylabel(ylabel)
        polish(ax)
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(
        handles,
        labels,
        loc="upper left",
        bbox_to_anchor=(0.18, 0.985),
        ncol=2,
        frameon=False,
        facecolor="none",
        edgecolor="none",
        handlelength=1.4,
        handletextpad=0.4,
        columnspacing=0.9,
        borderaxespad=0.0,
    )
    finish_two_panel(fig)
    savefig(fig, out_dir, "fig_ae_fs_capacity_window", formats)


def plot_fs_sharing(rows: list[dict[str, str]], profile: str, out_dir: Path, formats: list[str]) -> None:
    ratios = [0.0, 0.5, 0.8]
    shapes = ["leaf", "global"]
    shape_labels = {"leaf": "Leaf", "global": "Global"}
    colors = {"leaf": COLORS["teal"], "global": COLORS["blue"]}
    inv_ratio: dict[str, list[float]] = {shape: [] for shape in shapes}
    lat_ratio: dict[str, list[float]] = {shape: [] for shape in shapes}

    for shape in shapes:
        for shared_ratio in ratios:
            lifo = row(rows, profile, "2.3", sharing_shape=shape, shared_ratio=shared_ratio, policy="LIFO")
            fs = row(rows, profile, "2.3", sharing_shape=shape, shared_ratio=shared_ratio, policy="FS+LIFO")
            inv_ratio[shape].append(ratio(val(fs, "snoop_sum_total_inv_packets"), val(lifo, "snoop_sum_total_inv_packets")))
            lat_ratio[shape].append(ratio(val(fs, "host_weighted_avg_latency_ns"), val(lifo, "host_weighted_avg_latency_ns")))

    fig, axes = plt.subplots(1, 2, figsize=FIGSIZE)
    x = np.arange(len(ratios))
    width = 0.34
    for i, shape in enumerate(shapes):
        offset = (i - 0.5) * width
        axes[0].bar(x + offset, inv_ratio[shape], color=colors[shape], edgecolor="black", linewidth=0.8, width=width, label=shape_labels[shape])
        axes[1].bar(x + offset, lat_ratio[shape], color=colors[shape], edgecolor="black", linewidth=0.8, width=width)
    for ax, ylabel in [(axes[0], "Norm. BISnp count"), (axes[1], "Norm. latency")]:
        ax.set_xlabel("Shared data (%)")
        ax.set_xticks(x, ["0", "50", "80"])
        ax.set_ylim(0.60, 1.18)
        ax.set_ylabel(ylabel)
        polish(ax)

    handles = [Patch(facecolor=colors[shape], edgecolor="black", linewidth=0.8, label=shape_labels[shape]) for shape in shapes]
    fig.legend(
        handles,
        [shape_labels[shape] for shape in shapes],
        loc="upper right",
        bbox_to_anchor=(0.985, 0.965),
        ncol=2,
        frameon=False,
        facecolor="none",
        edgecolor="none",
        handlelength=1.15,
        handleheight=1.08,
        handletextpad=0.35,
        columnspacing=0.9,
        borderaxespad=0.0,
        borderpad=0.15,
        handler_map={Patch: square_patch_handler()},
    )
    finish_two_panel(fig)
    savefig(fig, out_dir, "fig_ae_fs_sharing_scope", formats)


def generate_all(args: argparse.Namespace) -> None:
    import_plotting()
    setup_style()
    csv_path = first_existing(args.csv, "*_v2/*_v2_results.csv") if args.csv == DEFAULT_CSV else args.csv
    footprint_csv_path = (
        first_existing(args.footprint_csv, "*_12_rerun/*_12_plot_points.csv")
        if args.footprint_csv == DEFAULT_FOOTPRINT_CSV
        else args.footprint_csv
    )
    rows = load_rows(csv_path)
    footprint_rows = load_rows(footprint_csv_path)
    plot_placement_perfect(rows, args.profile, args.out_dir, args.formats)
    plot_footprint_regimes(footprint_rows, args.out_dir, args.formats)
    plot_group_size(rows, args.profile, args.out_dir, args.formats)
    plot_sf_placement(rows, args.profile, args.out_dir, args.formats)
    plot_fs_capacity(rows, args.profile, args.out_dir, args.formats)
    plot_fs_sharing(rows, args.profile, args.out_dir, args.formats)


def main() -> int:
    args = parse_args()
    if args.list:
        print("\n".join(FIGURE_NAMES))
        return 0
    generate_all(args)
    print(f"Wrote {len(FIGURE_NAMES)} AE figures to {args.out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
