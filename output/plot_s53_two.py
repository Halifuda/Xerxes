#!/usr/bin/env python3
"""Section 5.3 figures 19 and 21, redrawn to the 2026-11-01 spec.

fig0609_sf_policy_onoff  sigma = 1, cache = SF = 3,280 lines (the paper's
                         working point), five victim-selection policies on
                         three metrics, normalized to FIFO-off. Drawn by
                         plot_s53_figures_rev.plot_policy_baseline().
fig0609_invblk_bus_cost  InvBlk length N x ClnEvct off/on at bus delay
                         d = 8, 2 hosts, LIFO (left latency, right BISnp).

Same canvas (5.25 x 1.80 in) and style as plot_s54_six.py so the nine figures
of the review PDF share one layout.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import plot_ae_figures_rev as base
from plot_ae_figures_rev import FIGSIZE, finish_two_panel, load_rows, num, polish, savefig, setup_style

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT.parent / "revision" / "figures-rev"
SIGMA_CSV = ROOT / "output" / "fig13_sigma" / "summary.csv"
IBCLN_CSV = ROOT / "output" / "fig13_ibcln" / "summary.csv"

OFF_C = "#C0C0C0"
ON_C = "#00A0FF"
POLICY_ORDER = ["FIFO", "LRU", "LFI", "LIFO", "MRU"]


def fig_policy() -> None:
    """Fig. 19 is owned by plot_s53_figures_rev.plot_policy_baseline().

    That function draws the paper's working point (sigma = 1, cache = SF =
    3,280 lines), where ClnEvct on/off are byte-identical and the five
    policies order the way v3 reported (LFI/LIFO clearly ahead, MRU not the
    worst). The sigma = 2 slice drew LIFO into fourth place, which is a real
    effect but belongs to Fig. 20, not here.
    """
    import plot_s53_figures_rev as baseline

    baseline.base.import_plotting()
    baseline.setup_style()
    baseline.plot_policy_baseline()


def fig_invblk() -> None:
    plt = base.plt
    rows = [r for r in load_rows(IBCLN_CSV) if r["group"] == "m"]
    bursts = sorted({int(float(r["burst"])) for r in rows})
    cell = {(r["delay_per_T"], r["clean"], int(float(r["burst"]))): r for r in rows}
    # Only the bus-cost geometry (d = 8). d = 0 is the v3 configuration and is
    # no longer plotted: with a zero-cost bus the curve is monotone and the
    # N = 2 minimum disappears, so mixing the two hides the point of the figure.
    series = [
        ("8.0", "False", "#00A0FF", "-", "o", "d = 8, off"),
        ("8.0", "True", "#00A0FF", "--", "s", "d = 8, on"),
    ]
    fig, axes = plt.subplots(1, 2, figsize=FIGSIZE)
    for delay, clean, color, ls, marker, label in series:
        lat = [num(cell[(delay, clean, b)], "avg_latency_ns") for b in bursts]
        bis = [num(cell[(delay, clean, b)], "bisnp") for b in bursts]
        for ax, vals in ((axes[0], lat), (axes[1], bis)):
            ax.plot(range(len(bursts)), vals, color=color, linestyle=ls, marker=marker,
                    markersize=4.2, linewidth=1.3, markeredgecolor="black",
                    markeredgewidth=0.6, label=label)

    for ax, lab in zip(axes, ("Latency (ns)", "BISnp")):
        ax.set_xticks(range(len(bursts)), [str(b) for b in bursts])
        ax.set_xlabel("InvBlk length N", fontsize=10)
        ax.set_ylabel(lab, fontsize=10)
        ax.tick_params(axis="x", labelsize=10)
        polish(ax)

    lat = [num(cell[(d, c, b)], "avg_latency_ns") for d, c, *_ in series for b in bursts]
    bis = [num(cell[(d, c, b)], "bisnp") for d, c, *_ in series for b in bursts]
    lo, hi = min(lat), max(lat)
    axes[0].set_ylim(lo - 0.09 * (hi - lo), hi + 0.10 * (hi - lo))
    axes[1].set_ylim(0, max(bis) * 1.10)

    handles = [ln for ax in axes[:1] for ln in ax.get_lines()]
    fig.legend(handles, [h.get_label() for h in handles], loc="upper center",
               bbox_to_anchor=(0.5, 0.95), ncol=2, frameon=False, fontsize=8,
               handlelength=1.6, handletextpad=0.35, columnspacing=1.1, borderaxespad=0.0)
    fig.text(0.5, 1.00, "2 hosts, LIFO, cache 3,280 lines, 30k requests", ha="center",
             va="top", fontsize=9)
    finish_two_panel(fig)
    fig.subplots_adjust(top=0.70)
    savefig(fig, OUT, "fig0609_invblk_bus_cost", ["jpg", "eps", "png"])


def main() -> int:
    base.import_plotting()
    setup_style()
    fig_policy()
    fig_invblk()
    print("wrote fig0609_sf_policy_onoff and fig0609_invblk_bus_cost to", OUT)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
