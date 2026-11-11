#!/usr/bin/env python3
"""Fig. 19 (right panel), small-cache rerun: MemClnEvct on/off per replacement policy.

Data: Xerxes/results/repro/s53_cln_small/<POL>_<off|on>/main.csv
  10 cells = {FIFO,LRU,LFI,LIFO,MRU} x {ClnEvct off, on}, host cache 512 lines,
  SF 1,024 lines (assoc 8), 30,000 requests, 90/10 hot/cold, seed 10000.

Panel: for each policy two bars, latency(on/off) in grey and invalidation(on/off)
in blue, log y from 0.05 to 1.5 with the parity line at 1.0.
"""

from __future__ import annotations

import csv
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import plot_ae_figures_rev as P  # noqa: E402
import matplotlib.ticker as mticker  # noqa: E402

P.import_plotting()
plt = P.plt
np = P.np

ROOT = Path(__file__).resolve().parents[1]          # .../Xerxes
RUN = ROOT / "results/repro/s53_cln_small"
REPO = ROOT.parent
OUT = REPO / "revision" / "figures-rev"
NAME = "fig19_right_smallcache"
FORMATS = ["png", "eps", "jpg"]

POL5 = ["FIFO", "LRU", "LFI", "LIFO", "MRU"]
OFF_GREY = "#9E9E9E"
ON_BLUE = "#00A0FF"


def read_main(path: Path) -> dict[str, float]:
    """metric,value rows only; main.csv also appends text stats and event traces."""
    out: dict[str, float] = {}
    with path.open(newline="") as fh:
        for row in csv.DictReader(fh):
            key = (row.get("metric") or "").strip()
            val = (row.get("value") or "").strip()
            if not key or not val:
                continue
            try:
                out[key] = float(val)
            except ValueError:
                continue
    return out


def cell(pol: str, arm: str) -> dict[str, float]:
    d = read_main(RUN / f"{pol}_{arm}" / "main.csv")
    return {
        "lat": d["Host-0:avg_latency_ns"] / 1000.0,        # us
        "inv": d["Snoop:total_inv_packets"],
        "sent": d.get("Host-0:cln_evct_sent", 0.0),
        "stale": d.get("Snoop:cln_evct_stale", 0.0),
        "clean": d.get("Host-0:cache_clean_evict_count", 0.0),
        "req": d.get("Host-0:req_count", 0.0),
        "hit": d.get("Host-0:cache_hit_count", 0.0),
    }


def fig_right() -> None:
    off = {p: cell(p, "off") for p in POL5}
    on = {p: cell(p, "on") for p in POL5}
    lat_r = [on[p]["lat"] / off[p]["lat"] for p in POL5]
    inv_r = [on[p]["inv"] / off[p]["inv"] for p in POL5]
    sent_tot = int(round(sum(on[p]["sent"] for p in POL5)))

    P.setup_style()
    P.mpl.rcParams.update({"axes.labelsize": 6.2, "xtick.labelsize": 5.6,
                           "ytick.labelsize": 5.6, "axes.linewidth": 0.8})
    fig, ax = plt.subplots(figsize=(2.2, 1.8))
    x = np.arange(len(POL5))
    w = 0.32
    bars_l = ax.bar(x - w / 2, lat_r, width=w, color=OFF_GREY, edgecolor="black",
                    linewidth=0.35, zorder=3, label="latency")
    bars_i = ax.bar(x + w / 2, inv_r, width=w, color=ON_BLUE, edgecolor="black",
                    linewidth=0.35, zorder=3, label="invalidation")

    ax.set_yscale("log")
    ax.set_ylim(0.05, 2.2)
    ax.set_yticks([0.05, 0.1, 0.25, 0.5, 1.0])
    ax.set_yticklabels(["0.05", "0.1", "0.25", "0.5", "1.0"])
    ax.yaxis.set_minor_locator(mticker.NullLocator())
    ax.yaxis.set_major_formatter(mticker.FixedFormatter(
        ["0.05", "0.1", "0.25", "0.5", "1.0"]))
    ax.axhline(1.0, color="0.55", lw=0.7, ls=(0, (2, 2)), zorder=2)

    ax.set_xticks(x)
    ax.set_xticklabels(POL5, fontsize=5.4)
    ax.set_xlim(-0.62, len(POL5) - 0.38)
    ax.set_ylabel("ClnEvct on / off", fontsize=6.2, labelpad=1.5)
    ax.tick_params(axis="y", width=0.8, length=2.4, pad=1.6)
    ax.tick_params(axis="x", width=0.8, length=2.4, pad=1.8)
    for s in ax.spines.values():
        s.set_color("black")
        s.set_linewidth(0.8)

    for rect, val in zip(bars_l, lat_r):
        ax.text(rect.get_x() + rect.get_width() / 2, val * 1.045, f"{val:.2f}",
                ha="center", va="bottom", fontsize=4.2, rotation=90, color="0.12")
    for rect, val in zip(bars_i, inv_r):
        ax.text(rect.get_x() + rect.get_width() / 2, val * 1.045, f"{val:.3f}",
                ha="center", va="bottom", fontsize=3.9, rotation=90, color="0.12")

    ax.legend(loc="upper right", bbox_to_anchor=(0.975, 0.995), fontsize=4.0,
              frameon=False, handlelength=0.8, handletextpad=0.3,
              labelspacing=0.15, borderpad=0.0, borderaxespad=0.0)
    ax.text(0.015, 0.995,
            "cache 512 lines, SF 1,024 lines,\n" + f"{sent_tot:,} notifications",
            transform=ax.transAxes, fontsize=4.0, va="top", ha="left",
            color="0.25", linespacing=1.3)

    fig.subplots_adjust(left=0.15, right=0.985, bottom=0.145, top=0.985)
    P.savefig(fig, OUT, NAME, FORMATS)
    print(f"wrote {OUT / (NAME + '.png')} etc.")
    for p, lr, ir in zip(POL5, lat_r, inv_r):
        print(f"  {p:5s} lat {off[p]['lat']:.4f} -> {on[p]['lat']:.4f} us "
              f"({lr:.5f})  inv {off[p]['inv']:.0f} -> {on[p]['inv']:.0f} "
              f"({ir:.5f})  sent {on[p]['sent']:.0f}")
    print(f"  notifications total = {sent_tot}")


def main() -> None:
    fig_right()


if __name__ == "__main__":
    main()
