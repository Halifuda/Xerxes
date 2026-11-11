
#!/usr/bin/env python3
"""Fig 19 / Fig 20 for the v4 draft, with the MemClnEvct axis (2026-11-09).

Fig 19  policy experiment, single endpoint, five policies, ClnEvct on/off.
Fig 20  block-length experiment, two requesters sharing a 256 KiB stream
        (1.25x the 3,280-line cache), block length 1/2/3/4/8, ClnEvct on/off.

Data
  results/repro/s53/policy_V_<POL>/main.csv          (off)
  results/repro_s53_on/s53/policy_V_<POL>/main.csv   (on)
  results/rev_53_invblk_v3/step13_fp256_cln/n<N>_<state>_s<S>/main.csv
"""
from __future__ import annotations

import argparse
import csv
import statistics as st
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
REPO = ROOT.parent
FIGS = REPO / "revision" / "figures-rev"
RES = ROOT / "results"
P53 = RES / "repro" / "s53"
P53_ON = RES / "repro_s53_on" / "s53"
INVBLK = RES / "rev_53_invblk_v3" / "step13_fp256_cln"
SMALL = RES / "repro" / "s53_cln_small"


def apply_layout(data_root: Path | None, out_dir: Path | None) -> None:
    """Point the readers at a reproduction root laid out by repro_fig16_23.py."""
    global P53, P53_ON, INVBLK, SMALL, FIGS
    if data_root is not None:
        P53 = data_root / "s53"
        P53_ON = data_root / "s53_on"
        INVBLK = data_root / "s53_invblk"
        SMALL = data_root / "s53_small"
    if out_dir is not None:
        FIGS = out_dir

sys.path.insert(0, str(HERE))
import plot_ae_figures_rev as P  # noqa: E402

P.import_plotting()
P.setup_style()
plt = P.plt
np = P.np

POL5 = ["FIFO", "LRU", "LFI", "LIFO", "MRU"]
POL_COLORS = {
    "FIFO": "#FFFFFF",
    "LRU": "#C0C0C0",
    "LFI": "#E6DC32",
    "LIFO": "#00C8C8",
    "MRU": "#00A0FF",
}
OFF_GREY = "#9E9E9E"
ON_BLUE = "#00A0FF"
FORMATS = ["eps", "jpg", "png"]
SEEDS = (1, 2, 3)


def read_main(path: Path) -> dict[str, float]:
    out: dict[str, float] = {}
    with path.open(newline="") as fh:
        for row in csv.DictReader(fh):
            name, val = row.get("metric"), row.get("value")
            if name is None or val in (None, ""):
                continue
            try:
                out[name] = float(val)
            except ValueError:
                continue
    return out


def policy_cell(root: Path, pol: str) -> dict[str, float]:
    m = read_main(root / f"policy_V_{pol}" / "main.csv")
    lat = [v for k, v in m.items() if k.endswith(":avg_latency_ns")]
    inv = sum(v for k, v in m.items() if k.endswith(":total_inv_packets"))
    sent = sum(v for k, v in m.items() if k.endswith(":cln_evct_sent"))
    clean = sum(v for k, v in m.items() if k.endswith(":cache_clean_evict_count"))
    return dict(lat=st.mean(lat), inv=inv, sent=sent, clean=clean)


def small_cell(pol: str, arm: str) -> dict[str, float]:
    """512-line cache / 1,024-line SF variant, where MemClnEvct does trigger."""
    m = read_main(SMALL / f"{pol}_{arm}" / "main.csv")
    return dict(lat=m["Host-0:avg_latency_ns"] / 1000.0,
                inv=m["Snoop:total_inv_packets"],
                sent=m.get("Host-0:cln_evct_sent", 0.0))


def invblk_cell(n: int, state: str, seed: int) -> dict[str, float]:
    m = read_main(INVBLK / f"n{n}_{state}_s{seed}" / "main.csv")
    lat = [v for k, v in m.items() if k.endswith(":avg_latency_ns")]
    inv = sum(v for k, v in m.items() if k.endswith(":total_inv_packets")) / 2.0
    sent = sum(v for k, v in m.items() if k.endswith(":cln_evct_sent"))
    stale = sum(v for k, v in m.items() if k.endswith(":cln_evct_stale"))
    return dict(lat=st.mean(lat), inv=inv, sent=sent, stale=stale)


def save(fig, name: str) -> None:
    for fmt in FORMATS:
        fig.savefig(FIGS / f"{name}.{fmt}", dpi=600, facecolor="white")
    plt.close(fig)


def style_panel(ax):
    ax.tick_params(labelsize=12.2)
    for s in ax.spines.values():
        s.set_linewidth(0.8)


def _sci_label(v, _pos):
    if v == 0:
        return "0"
    exp = int(np.floor(np.log10(abs(v))))
    mant = v / (10 ** exp)
    if abs(mant - round(mant)) < 1e-9:
        mant = int(round(mant))
    else:
        mant = f"{mant:.1f}"
    return f"{mant}e{exp}"


def sci_axis(ax):
    ax.yaxis.set_major_formatter(P.mpl.ticker.FuncFormatter(_sci_label))


def fig_policy() -> None:
    off = {p: policy_cell(P53, p) for p in POL5}
    on = {p: policy_cell(P53_ON, p) for p in POL5}
    fig, (ax, axr) = plt.subplots(
        1, 2, figsize=(7.2, 1.72), gridspec_kw={"width_ratios": [2.58, 1.62]}
    )
    metrics = [("lat", "Latency"), ("inv", "Invalidation count")]
    width = 0.165
    base = {m: off["FIFO"][m] for m, _ in metrics}
    xs = range(len(POL5))
    for gi, (key, label) in enumerate(metrics):
        for pi, pol in enumerate(POL5):
            x = gi * 1.0 + (pi - 2) * width
            ax.bar(
                x,
                off[pol][key] / base[key],
                width=width * 0.9,
                color=POL_COLORS[pol],
                edgecolor="black",
                linewidth=0.6,
                zorder=3,
            )
        ax.text(gi * 1.0, -0.075, label, ha="center", va="top", fontsize=12.6,
                transform=ax.get_xaxis_transform())
    ax.axhline(1.0, color="0.55", lw=0.8, ls=(0, (2, 2)), zorder=2)
    ax.set_xlim(-0.62, 1.62)
    ax.set_ylim(0.0, 1.30)
    ax.set_xticks([])
    ax.set_ylabel("Normalized value", fontsize=12.6)
    style_panel(ax)
    handles = [
        plt.Rectangle((0, 0), 1, 1, facecolor=POL_COLORS[p], edgecolor="black",
                      linewidth=0.6)
        for p in POL5
    ]
    ax.legend(handles, POL5, ncol=3, fontsize=11.4, frameon=False,
              loc="lower right", bbox_to_anchor=(1.0, 1.0),
              handlelength=1.1, columnspacing=0.8, handletextpad=0.4,
              borderaxespad=0.0)

    # right panel: same five policies with a 512-line cache and a 1,024-line SF,
    # so the requester does self-evict clean lines and MemClnEvct actually fires
    small_off = {p: small_cell(p, "off") for p in POL5}
    small_on = {p: small_cell(p, "on") for p in POL5}
    lat_r = [small_on[p]["lat"] / small_off[p]["lat"] for p in POL5]
    inv_r = [small_on[p]["inv"] / small_off[p]["inv"] for p in POL5]
    xr = range(len(POL5))
    bw = 0.34
    axr.bar([x - bw / 2 for x in xr], lat_r, width=bw, color=OFF_GREY,
            edgecolor="black", linewidth=0.5, zorder=3)
    axr.bar([x + bw / 2 for x in xr], inv_r, width=bw, color=ON_BLUE,
            edgecolor="black", linewidth=0.5, zorder=3)
    axr.axhline(1.0, color="0.55", lw=0.8, ls=(0, (2, 2)), zorder=2)
    axr.set_yscale("log")
    axr.set_ylim(0.1, 1.30)
    axr.set_yticks([0.1, 0.25, 0.5, 1.0])
    axr.set_yticklabels(["0.1", "0.25", "0.5", "1.0"])
    axr.minorticks_off()
    axr.set_xticks(list(xr))
    axr.set_xticklabels(POL5, rotation=45, ha="right", fontsize=11.0)
    axr.set_xlim(-0.6, len(POL5) - 0.4)
    axr.set_ylabel("ClnEvct on / off", fontsize=12.0, labelpad=1.5)
    style_panel(axr)
    axr.legend(
        [plt.Rectangle((0, 0), 1, 1, facecolor=OFF_GREY, edgecolor="black",
                       linewidth=0.5),
         plt.Rectangle((0, 0), 1, 1, facecolor=ON_BLUE, edgecolor="black",
                       linewidth=0.5)],
        ["latency", "invalidations"], ncol=1, fontsize=10.6, frameon=False,
        loc="lower left", bbox_to_anchor=(-0.02, 1.0),
        handlelength=1.1, columnspacing=1.0, handletextpad=0.4,
        borderaxespad=0.0,
    )
    fig.subplots_adjust(left=0.11, right=0.99, bottom=0.33, top=0.68, wspace=0.38)
    save(fig, "fig_repro_s53_policy")


def fig_invblk() -> None:
    ns = [1, 2, 3, 4, 8]
    data = {}
    for state in ("off", "on"):
        for n in ns:
            rows = [invblk_cell(n, state, s) for s in SEEDS
                    if (INVBLK / f"n{n}_{state}_s{s}" / "main.csv").is_file()]
            data[(n, state)] = {
                "lat": [r["lat"] for r in rows],
                "inv": [r["inv"] for r in rows],
                "sent": [r["sent"] for r in rows],
                "stale": [r["stale"] for r in rows],
            }
    fig, axes = plt.subplots(1, 3, figsize=(7.1, 1.82))
    for ax, key, ylabel in (
        (axes[0], "lat", "Latency (µs)"),
        (axes[1], "inv", "Invalidations"),
    ):
        for state, color, marker, label, dx in (
            ("off", OFF_GREY, "o", "ClnEvct off", 0.0),
            ("on", ON_BLUE, "s", "ClnEvct on", 0.0),
        ):
            xs, ys, lo, hi = [], [], [], []
            for i, n in enumerate(ns):
                vals = data[(n, state)][key]
                mean = st.mean(vals)
                if key == "lat":
                    mean /= 1000.0
                    lo.append(min(vals) / 1000.0)
                    hi.append(max(vals) / 1000.0)
                else:
                    lo.append(min(vals))
                    hi.append(max(vals))
                xs.append(i + dx)
                ys.append(mean)
            ax.errorbar(
                xs, ys, yerr=[np.subtract(ys, lo), np.subtract(hi, ys)],
                color=color, lw=1.2, marker=marker,
                ms=6.0 if state == "off" else 3.6,
                mfc=color, mec="black", mew=0.5, capsize=1.8,
                elinewidth=0.7, label=label, zorder=3,
            )
        ax.set_xticks(range(len(ns)))
        ax.set_xticklabels([str(n) for n in ns], fontsize=11.0)
        ax.set_xlabel("InvBlk length N", fontsize=11.8)
        ax.set_ylabel(ylabel, fontsize=11.8)
        ax.set_ylim(bottom=0)
        ax.set_xlim(-0.5, len(ns) - 0.5)
        style_panel(ax)

    axes[0].set_ylim(0, 2.30)
    axes[0].set_yticks([0, 1, 2])
    axes[1].set_ylim(0, 18000)
    axes[1].set_yticks([0, 5000, 10000, 15000])
    axes[1].set_ylabel("Invalidations", fontsize=11.8, labelpad=1.0)
    sci_axis(axes[1])
    legend_handles = [
        P.mpl.lines.Line2D([0], [0], color=OFF_GREY, lw=1.2, marker="o",
                           ms=6.0, mfc=OFF_GREY, mec="black", mew=0.5,
                           label="ClnEvct off"),
        P.mpl.lines.Line2D([0], [0], color=ON_BLUE, lw=1.2, marker="s",
                           ms=3.6, mfc=ON_BLUE, mec="black", mew=0.5,
                           label="ClnEvct on"),
    ]
    axes[0].legend(handles=legend_handles, fontsize=10.4, frameon=False,
                   loc="lower left", ncol=1, handletextpad=0.3,
                   borderaxespad=0.3)

    # panel (c): the ClnEvct axis itself, sent vs already-stale
    xs = []
    sent = []
    stale = []
    for i, n in enumerate(ns):
        xs.append(i)
        sent.append(st.mean(data[(n, "on")]["sent"]))
        stale.append(st.mean(data[(n, "on")]["stale"]))
    axes[2].bar(xs, sent, width=0.5, color=ON_BLUE, edgecolor="black",
                linewidth=0.6, zorder=3, label="valid")
    axes[2].bar(xs, stale, width=0.5, color="white", edgecolor="black",
                linewidth=0.6, hatch="////", zorder=4, label="stale")
    axes[2].set_xticks(range(len(ns)))
    axes[2].set_xticklabels([str(n) for n in ns], fontsize=11.0)
    axes[2].set_xlabel("InvBlk length N", fontsize=11.8)
    axes[2].set_ylabel("ClnEvct", fontsize=11.8)
    axes[2].set_ylim(0, max(sent) * 1.22)
    axes[2].set_yticks([0, 2000, 4000])
    sci_axis(axes[2])
    axes[2].legend(fontsize=9.8, frameon=False, ncol=2, loc="lower left",
                   bbox_to_anchor=(0.0, 1.0), handlelength=1.0,
                   handletextpad=0.35, columnspacing=0.8,
                   borderaxespad=0.0)
    style_panel(axes[2])
    fig.subplots_adjust(left=0.10, right=0.995, bottom=0.30, top=0.74,
                        wspace=0.55)
    save(fig, "fig_repro_s53_invblk")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-root", type=Path, default=None,
                        help="root holding s53/, s53_on/, s53_invblk/, s53_small/")
    parser.add_argument("--out-dir", type=Path, default=None)
    parser.add_argument("--only", default="policy,invblk",
                        help="comma-separated: policy,invblk")
    args = parser.parse_args()
    apply_layout(args.data_root, args.out_dir)
    wanted = {x.strip() for x in args.only.split(",") if x.strip()}
    if "policy" in wanted:
        fig_policy()
    if "invblk" in wanted:
        fig_invblk()
    print("wrote", ", ".join(sorted(wanted)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
