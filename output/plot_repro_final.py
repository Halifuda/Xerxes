
#!/usr/bin/env python3
"""Final-version figures for the reproduction package (2026-11-03).

Five figures, one per experiment family:
  fig_repro_s53_policy    s53 policy knob (V arm, 5 policies, latency + inv. count)
  fig_repro_s53_invblk    s53 invalidation-block length, one configuration, v3's
                          three metrics (bandwidth / latency / wait on invalidation)
  fig_repro_s54_degree    s54 shared-participation axis (n = 1/4/8/16)
  fig_repro_s54_capacity  s54 SF-capacity axis (7 capacities, 0.22x - 3.56x footprint)
  fig_repro_s54_ratio     s54 shared-data axis
"""
from __future__ import annotations

import argparse
import csv
import json
import statistics as st
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent                      # .../Xerxes
REPO = ROOT.parent                      # repo root
REV = REPO / "revision"
FIGS = REV / "figures-rev"
RES = ROOT / "results"
OUT = RES / "repro" / "s54"
OUT53 = RES / "repro" / "s53"
OUT53_ON = RES / "repro_s53_on" / "s53"
CAP2 = RES / "rev_54_wl2" / "_x_cap2"
SD = RES / "rev_54_wl2" / "_x_sd"
CAP_OVERRIDE: Path | None = None


def apply_layout(data_root: Path | None, cap_dir: Path | None, ratio_dir: Path | None,
                 out_dir: Path | None) -> None:
    global OUT, OUT53, OUT53_ON, CAP2, SD, CAP_OVERRIDE, FIGS
    if data_root is not None:
        OUT = data_root
        OUT53 = data_root
        OUT53_ON = data_root
    if cap_dir is not None:
        CAP_OVERRIDE = cap_dir
    if ratio_dir is not None:
        SD = ratio_dir
    if out_dir is not None:
        FIGS = out_dir

sys.path.insert(0, str(HERE))
import plot_ae_figures_rev as P  # noqa: E402

P.import_plotting()
plt, np = P.plt, P.np

POL5 = ["FIFO", "LRU", "LFI", "LIFO", "MRU"]
POL_COLORS = {
    "FIFO": "#FFFFFF",
    "LRU": "#C0C0C0",
    "LFI": "#E6DC32",
    "LIFO": "#00C8C8",
    "MRU": "#00A0FF",
    "FS+LIFO": "#00A0FF",
    "LIFO#": "#00C8C8",
    "FS+FIFO": "#00A0FF",
}
GREY = "#C0C0C0"
CYAN = "#00C8C8"
BLUE = "#00A0FF"
FORMATS = ["eps", "jpg", "png"]


# ---------------------------------------------------------------- readers
def read_main(path: Path) -> dict[str, float]:
    out: dict[str, float] = {}
    with path.open(newline="") as fh:
        for row in csv.DictReader(fh):
            key, value = row.get("metric"), row.get("value")
            if not key or value is None:
                continue
            try:
                out[key] = float(value)
            except ValueError:
                continue
    return out


def hosts_mean(metrics: dict[str, float], key: str) -> float:
    vals = [v for k, v in metrics.items()
            if k.endswith(":" + key) and (k.startswith("Host-") or k.startswith("Snoop"))]
    return st.mean(vals) if vals else 0.0


def wl2_cell(path: Path) -> dict[str, dict[str, float]]:
    with (path / "wl2_results.csv").open(newline="") as fh:
        return {r["policy"]: r for r in csv.DictReader(fh)}


def cell_stats(path: Path) -> dict[str, dict[str, float]]:
    rows = wl2_cell(path)
    out = {}
    for pol, r in rows.items():
        out[pol] = {
            "inv": float(r["snoop_sum_total_inv_packets"]) / 16.0,
            "evict": float(r["snoop_sum_evict_total_count"]) / 16.0,
            "lat": float(r["host_weighted_avg_latency_ns"]) / 1000.0,
            "bw": float(r["host_sum_bw_gbps"]),
        }
    return out


def seeds_stats(pattern_dir: Path, stems: list[str], seeds=(10000, 20000, 30000)):
    """Return {stem: {pol: {metric: (mean, lo, hi)}}}."""
    acc: dict[str, dict[str, dict[str, list[float]]]] = {}
    for stem in stems:
        for seed in seeds:
            d = pattern_dir / f"{stem}_s{seed}"
            if not (d / "wl2_results.csv").is_file():
                continue
            for pol, m in cell_stats(d).items():
                slot = acc.setdefault(stem, {}).setdefault(pol, {})
                for k, v in m.items():
                    slot.setdefault(k, []).append(v)
    out = {}
    for stem, pols in acc.items():
        out[stem] = {}
        for pol, metrics in pols.items():
            out[stem][pol] = {k: (st.mean(v), min(v), max(v)) for k, v in metrics.items()}
    return out


# ---------------------------------------------------------------- figure 1
def fig_policy() -> Path:
    rows = P.load_rows(ROOT / "output" / "fig13_close" / "summary.csv")
    arms = [r for r in rows if r["group"] == "a" and r["notify"] == "False"]
    table = {r["policy"]: r for r in arms}
    assert set(POL5) <= set(table), sorted(table)

    # Bandwidth is deliberately not plotted: with a fixed request count it is
    # delivered bytes / makespan, and a policy that misses less delivers fewer
    # bytes in proportionally less time, so it lands inside 2.1% for all five
    # policies (v3's own bandwidth bars are equally flat, 1.00-1.06).
    metrics = [("Latency", "avg_latency_ns"), ("Invalidation count", "bisnp")]
    vals = {name: [float(table[p][key]) for p in POL5] for name, key in metrics}

    fig, ax = plt.subplots(figsize=(5.25, 1.90))
    ngroup = len(metrics)
    width = 0.165
    x = np.arange(ngroup)
    for gi, (name, _) in enumerate(metrics):
        base = vals[name][0]
        for pi, pol in enumerate(POL5):
            v = vals[name][pi] / base
            xpos = gi + (pi - (len(POL5) - 1) / 2) * width
            ax.bar(xpos, v, width=width * 0.92, color=POL_COLORS[pol],
                   edgecolor="black", linewidth=0.6, zorder=3,
                   label=pol if gi == 0 else None)
            if pi >= 1:
                ax.text(xpos, v + 0.03, "%.2f" % v, ha="center", va="bottom",
                        fontsize=8.4, clip_on=False)
    ax.axhline(1.0, color="#808080", linestyle=(0, (1.5, 4.0)), linewidth=0.8, zorder=2)
    ax.set_xticks(x)
    ax.set_xticklabels([n for n, _ in metrics])
    ax.set_ylabel("Normalized value")
    ax.set_ylim(0, 1.18)
    P.polish(ax)
    ax.legend(ncol=5, loc="lower center", bbox_to_anchor=(0.5, 1.01),
              frameon=False, handlelength=1.0, handletextpad=0.5, columnspacing=0.9,
              fontsize=10.0)
    fig.subplots_adjust(left=0.135, right=0.99, bottom=0.235, top=0.775)
    out = FIGS / "fig_repro_s53_policy"
    P.savefig(fig, FIGS, out.name, FORMATS)
    return out.with_suffix(".png")


# ---------------------------------------------------------------- figure 2
def fig_invblk() -> Path:
    """v3's Figure 20 in its own shape: one configuration, three metrics.

    Configuration: the "same" InvBlk arm (2 hosts, cache 3,280 lines, SF 6,560
    lines, assoc 8, LIFO, bus delay 8) so that every panel describes the same
    experiment.  v3 normalises to length = 1; we keep that convention and add
    the ClnEvct on/off pair, which v3 could not show (its notification knob was
    dead at this working point).
    """
    NS = (1, 2, 4, 8)

    def one(root: Path, n: int) -> dict[str, float]:
        m = read_main(root / ("invblk_same-d8-n%d" % n) / "main.csv")
        hosts = sorted({k.split(":")[0] for k in m if k.startswith("Host-")})
        lat = st.mean([m["%s:avg_latency_ns" % h] for h in hosts
                       if "%s:avg_latency_ns" % h in m])
        wait = st.mean([m["%s:avg_evict_wait_ns" % h] for h in hosts
                        if "%s:avg_evict_wait_ns" % h in m])
        bw = sum(m.get("%s:bw_gbps" % h, 0.0) for h in hosts)
        return {"lat": lat, "wait": wait, "bw": bw}

    states = [("ClnEvct off", OUT53, GREY, "off"),
              ("ClnEvct on", OUT53_ON, BLUE, "on")]
    data = {tag: {n: one(root, n) for n in NS
                  if (root / ("invblk_same-d8-n%d" % n) / "main.csv").is_file()}
            for _label, root, _c, tag in states}
    assert data["off"], "missing repro/s53 invblk same-d8 cells"
    base = data["off"][1]

    panels = [("Bandwidth", "bw", None), ("Latency", "lat", None),
              ("Wait on inv.", "wait", None)]
    fig, axes = plt.subplots(1, 3, figsize=(5.30, 2.20))
    width = 0.36
    x = np.arange(len(NS))
    for ai, (title, key, ymax) in enumerate(panels):
        for si, (label, _root, color, tag) in enumerate(states):
            vals = [data[tag][n][key] / base[key] for n in NS]
            pos = x + (si - 0.5) * width
            axes[ai].bar(pos, vals, width * 0.94, color=color, edgecolor="black",
                         linewidth=0.6, zorder=3, label=label if ai == 0 else None)
            if key in ("lat", "wait"):
                for xp, v in zip(pos, vals):
                    axes[ai].text(xp, v + 0.02, "%.2f" % v, ha="center", va="bottom",
                                  fontsize=7.8, rotation=90)
        axes[ai].axhline(1.0, color="#808080", linestyle=(0, (1.5, 4.0)),
                         linewidth=0.8, zorder=2)
        axes[ai].set_title(title, fontsize=10.0, pad=2.5)
        axes[ai].set_xlabel("InvBlk length N")
        axes[ai].set_xticks(x)
        axes[ai].set_xticklabels([str(n) for n in NS])
        top = ymax if ymax else max(data[tag][n][key] / base[key]
                                    for tag in data for n in NS) * 1.22
        axes[ai].set_ylim(0, top)
        P.polish(axes[ai])
        axes[ai].yaxis.label.set_size(10.5)
        if ai == 0:
            axes[ai].set_ylabel("Normalized")
            axes[ai].yaxis.label.set_size(9.6)
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="upper center", bbox_to_anchor=(0.5, 0.995),
               ncol=2, frameon=False, fontsize=9.0, handlelength=1.2,
               handletextpad=0.4, columnspacing=1.6)
    fig.subplots_adjust(left=0.105, right=0.99, bottom=0.205, top=0.755, wspace=0.42)
    out = FIGS / "fig_repro_s53_invblk"
    P.savefig(fig, FIGS, out.name, FORMATS)
    return out.with_suffix(".png")


# ---------------------------------------------------------------- figure 3
def fig_degree() -> Path:
    all_stems = ["deg_n1", "deg_n4", "deg_n8", "deg_n16"]
    stems = [s for s in all_stems
             if any((OUT / f"{s}_s{seed}").is_dir() for seed in (10000, 20000, 30000))]
    assert stems, f"no degree cells under {OUT}"
    acc = seeds_stats(OUT, stems)
    ns = [int(s.rsplit("n", 1)[1]) for s in stems]
    bare = {}
    for n in ns:
        tag = "ctl_nullsf_n%d_s10000" % n
        d = OUT / tag
        if (d / "wl2_results.csv").is_file():
            bare[n] = cell_stats(d)["LIFO"]["lat"]

    fig, axes = plt.subplots(1, 2, figsize=(5.30, 1.98))
    for pol, color, ls, marker in (("LIFO", "#404040", "-", "o"), ("FS+LIFO", BLUE, "--", "s")):
        lat = [acc[s][pol]["lat"] for s in stems]
        axes[0].errorbar(ns, [v[0] for v in lat],
                         yerr=[[v[0] - v[1] for v in lat], [v[2] - v[0] for v in lat]],
                         ls=ls, marker=marker, color=color, markersize=4.5, linewidth=1.3,
                         capsize=2.5, label=pol)
        inv = [acc[s][pol]["inv"] for s in stems]
        axes[1].errorbar(ns, [v[0] for v in inv],
                         yerr=[[v[0] - v[1] for v in inv], [v[2] - v[0] for v in inv]],
                         ls=ls, marker=marker, color=color, markersize=4.5, linewidth=1.3,
                         capsize=2.5, label=pol)
    if bare:
        xs = sorted(bare)
        axes[0].plot(xs, [bare[x] for x in xs], ":", marker="^", color="#B0B0B0",
                     markersize=4, linewidth=1.1, label="bare (no SF)")
    axes[0].set_xlabel("Shared participants n")
    axes[0].set_ylabel("Latency (\u00b5s)")
    axes[1].set_xlabel("Shared participants n")
    axes[1].set_ylabel("Invalidations")
    axes[1].set_ylim(bottom=0)
    for ax in (axes[0], axes[1]):
        # linear x: the four participant counts are evenly readable and the
        # reader no longer has to guess that the spacing is log2
        ax.set_xticks(ns)
        ax.set_xticklabels([str(n) for n in ns])
        ax.minorticks_off()
        ax.set_xlim(-1.6, 18.6)
    axes[0].legend(frameon=False, fontsize=8.8, loc="upper left", handlelength=1.7,
                   handletextpad=0.4, borderaxespad=0.2)
    for ax in axes:
        P.polish(ax)
    fig.subplots_adjust(left=0.155, right=0.99, bottom=0.245, top=0.965, wspace=0.62)
    out = FIGS / "fig_repro_s54_degree"
    P.savefig(fig, FIGS, out.name, FORMATS)
    return out.with_suffix(".png")


# ---------------------------------------------------------------- figure 4
CAP_SF = (128, 256, 384, 448, 512, 544, 576, 640, 768, 1024, 2048)
FOOTPRINT = 576.0          # shared lines a host actually touches, r = 0.5


def _cap_series() -> dict[int, dict[str, dict[str, tuple[float, float, float]]]]:
    """{sf: {policy: {metric: (mean, lo, hi)}}} over the three seeds."""
    seeds = (10000, 20000, 30000)
    acc: dict[int, dict[str, dict[str, list[float]]]] = {}
    for sf in CAP_SF:
        for seed in seeds:
            # 256/512/2048 come from the 3-seed capacity sweep; the extra
            # points next to the 0.22/0.67/0.94 marks come from repro/s54.
            if CAP_OVERRIDE is not None:
                d = CAP_OVERRIDE / ("cap_sf%d_s%d" % (sf, seed))
            elif sf in (256, 512, 2048):
                d = CAP2 / ("a2_sf%d_s%d" % (sf, seed))
            else:
                d = OUT / ("cap_sf%d_s%d" % (sf, seed))
            if not (d / "wl2_results.csv").is_file():
                continue
            for pol, m in cell_stats(d).items():
                slot = acc.setdefault(sf, {}).setdefault(pol, {})
                for k, v in m.items():
                    slot.setdefault(k, []).append(v)
    return {sf: {pol: {k: (st.mean(v), min(v), max(v)) for k, v in metrics.items()}
                 for pol, metrics in pols.items()}
            for sf, pols in acc.items()}


def fig_capacity() -> Path:
    acc = _cap_series()
    sfs = sorted(acc)
    xs = [sf / FOOTPRINT for sf in sfs]
    fig, axes = plt.subplots(1, 2, figsize=(5.30, 1.98))
    for pol, color, ls, marker in (("LIFO", "#404040", "-", "o"),
                                   ("FS+LIFO", BLUE, "--", "s")):
        lat = [acc[sf][pol]["lat"] for sf in sfs]
        inv = [acc[sf][pol]["inv"] for sf in sfs]
        for ax, series in ((axes[0], lat), (axes[1], inv)):
            ax.errorbar(xs, [v[0] for v in series],
                        yerr=[[v[0] - v[1] for v in series],
                              [v[2] - v[0] for v in series]],
                        ls=ls, marker=marker, color=color, markersize=4.2,
                        linewidth=1.3, capsize=2.2, label=pol)
    ticks = [144, 288, 576, 1152, 2304]
    labels = ["0.25", "0.5", "1", "2", "4"]
    for ax in axes:
        ax.set_xscale("log")
        ax.set_xticks([t / FOOTPRINT for t in ticks])
        ax.set_xticklabels(labels)
        ax.minorticks_off()
        ax.set_xlim(0.18, 4.4)
        ax.set_xlabel("SF capacity / footprint")
    axes[0].set_ylabel("Latency (\u00b5s)")
    axes[1].set_ylabel("Invalidations")
    axes[1].set_ylim(bottom=0)
    axes[0].legend(frameon=False, fontsize=8.8, loc="upper right",
                   handlelength=1.7, handletextpad=0.4, borderaxespad=0.2)
    for ax in axes:
        P.polish(ax)
    fig.subplots_adjust(left=0.155, right=0.965, bottom=0.245, top=0.965, wspace=0.64)
    out = FIGS / "fig_repro_s54_capacity"
    P.savefig(fig, FIGS, out.name, FORMATS)
    return out.with_suffix(".png")


# ---------------------------------------------------------------- figure 5
def fig_ratio() -> Path:
    all_ratios = [0, 10, 25, 40, 50, 65, 80, 90]
    pairs = [(r, "a3_sf512_r%d" % r) for r in all_ratios]
    pairs = [(r, s) for r, s in pairs
             if any((SD / f"{s}_s{seed}").is_dir() for seed in (10000, 20000, 30000))]
    assert pairs, f"no ratio cells under {SD}"
    ratios = [r for r, _ in pairs]
    stems = [s for _, s in pairs]
    acc = seeds_stats(SD, stems)
    fig, axes = plt.subplots(1, 2, figsize=(5.30, 1.98))
    lat = {}
    for pol, color, ls, marker in (("LIFO", "#404040", "-", "o"), ("FS+LIFO", BLUE, "--", "s")):
        lat[pol] = [acc[s][pol]["lat"] for s in stems]
        axes[0].errorbar(ratios, [v[0] for v in lat[pol]],
                         yerr=[[v[0] - v[1] for v in lat[pol]], [v[2] - v[0] for v in lat[pol]]],
                         ls=ls, marker=marker, color=color, markersize=4.5, linewidth=1.3,
                         capsize=2.5, label=pol)
    cnt = [acc[s]["FS+LIFO"]["inv"][0] / acc[s]["LIFO"]["inv"][0]
           if acc[s]["LIFO"]["inv"][0] else float("nan") for s in stems]
    latr = [lat["FS+LIFO"][i][0] / lat["LIFO"][i][0] for i in range(len(stems))]
    axes[1].plot(ratios[1:], cnt[1:], "-", marker="o", color="#404040", markersize=4.5,
                 linewidth=1.3, label="invalidation count")
    axes[1].plot(ratios[1:], latr[1:], "--", marker="s", color=BLUE, markersize=4.5,
                 linewidth=1.3, label="latency")
    axes[1].axhline(1.0, color="#808080", linestyle=(0, (1.5, 4.0)), linewidth=0.8)
    for ax in axes:
        ax.set_xlabel("Shared data (%)")
        ax.set_xlim(-4, 94)
        ax.set_xticks([0, 25, 50, 75, 90])
    axes[0].set_ylabel("Latency (\u00b5s)")
    axes[1].set_ylabel("FS+LIFO / LIFO")
    axes[0].legend(frameon=False, fontsize=8.8, loc="upper left", handlelength=1.7,
                   handletextpad=0.4, borderaxespad=0.2)
    axes[1].legend(frameon=False, fontsize=8.8, loc="upper right", handlelength=1.7,
                   handletextpad=0.4, borderaxespad=0.2)
    axes[1].set_ylim(0.35, 1.25)
    for ax in axes:
        P.polish(ax)
    fig.subplots_adjust(left=0.155, right=0.985, bottom=0.245, top=0.965, wspace=0.62)
    out = FIGS / "fig_repro_s54_ratio"
    P.savefig(fig, FIGS, out.name, FORMATS)
    return out.with_suffix(".png")


def apply_style() -> None:
    P.setup_style()
    # Every graphic is placed at (or close to) full text width, so type is drawn
    # at its final size; keep it a little above the earlier 0.667x setting.
    plt.rcParams.update({
        "font.size": 10.6, "axes.labelsize": 11.4, "xtick.labelsize": 10.4,
        "ytick.labelsize": 10.6, "legend.fontsize": 9.9,
        "axes.titlesize": 11.6,
    })


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-root", type=Path, default=None,
                        help="directory holding deg_n*_s*/ and cap_sf*_s*/ cells")
    parser.add_argument("--cap-dir", type=Path, default=None,
                        help="directory holding cap_sf*_s*/ cells (overrides --data-root)")
    parser.add_argument("--ratio-dir", type=Path, default=None,
                        help="directory holding a3_sf512_r*_s*/ cells")
    parser.add_argument("--out-dir", type=Path, default=None)
    parser.add_argument("--only", default="degree,capacity,ratio",
                        help="comma-separated: degree,capacity,ratio")
    args = parser.parse_args()
    apply_layout(args.data_root, args.cap_dir, args.ratio_dir, args.out_dir)
    wanted = {x.strip() for x in args.only.split(",") if x.strip()}
    apply_style()
    made = []
    if "degree" in wanted:
        made.append(fig_degree())
    if "capacity" in wanted:
        made.append(fig_capacity())
    if "ratio" in wanted:
        made.append(fig_ratio())
    for p in made:
        print("wrote", p)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())