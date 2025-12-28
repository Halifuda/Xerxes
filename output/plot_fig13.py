#!/usr/bin/env python3
"""
Generate normalized bar charts (vs FIFO) for Bandwidth, Average latency, and Invalidation count
from requester stderr logs (.err). Minimal arguments: optional dir (default: output/fig13).

Usage:
  python scripts/plot_fig13.py [dir]
"""

import glob
import os
import re
import sys
from typing import Dict, Optional

import matplotlib.pyplot as plt
import pandas as pd

DEFAULT_DIR = "output/fig13"
BASELINE = "FIFO"
EVICT_RE = re.compile(r"Evict count:\s*([0-9.+\-eE]+)")
BW_RE = re.compile(r"Bandwidth \(GB/s\):\s*([0-9.+\-eE]+)")
LAT_RE = re.compile(r"Average latency \(ns\):\s*([0-9.+\-eE]+)")


def parse_err(path: str) -> Dict[str, Optional[float]]:
    metrics = {
        "evict_count": None,
        "bandwidth_gbs": None,
        "avg_latency_ns": None,
    }
    if not os.path.exists(path):
        return metrics

    with open(path, "r") as f:
        lines = [ln.strip() for ln in f]

    agg_section = False
    for ln in lines:
        if metrics["evict_count"] is None:
            m_ev = EVICT_RE.search(ln)
            if m_ev:
                try:
                    metrics["evict_count"] = float(m_ev.group(1))
                except ValueError:
                    metrics["evict_count"] = None
        if "Aggregate:" in ln:
            agg_section = True
            continue
        if not agg_section:
            continue
        m_bw = BW_RE.search(ln)
        if m_bw:
            try:
                metrics["bandwidth_gbs"] = float(m_bw.group(1))
            except ValueError:
                metrics["bandwidth_gbs"] = None
        m_lat = LAT_RE.search(ln)
        if m_lat:
            try:
                metrics["avg_latency_ns"] = float(m_lat.group(1))
            except ValueError:
                metrics["avg_latency_ns"] = None
    return metrics


def main():
    dir_path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_DIR
    out_dir = os.path.join(dir_path, "summary")
    fig_dir = dir_path
    os.makedirs(out_dir, exist_ok=True)

    err_files = sorted(glob.glob(os.path.join(dir_path, "*.err")))
    rows = []
    for err in err_files:
        base = os.path.splitext(os.path.basename(err))[0]
        metrics = parse_err(err)
        rows.append({"policy": base, **metrics})

    if not rows:
        raise SystemExit(f"No .err files found in {dir_path}")

    df = pd.DataFrame(rows)
    if BASELINE not in set(df["policy"]):
        raise SystemExit(f"Baseline '{BASELINE}' not found in policies: {sorted(df['policy'])}")
    base_row = df[df["policy"] == BASELINE].iloc[0]

    def norm(col: str):
        base_val = base_row[col]
        if base_val is None or pd.isna(base_val) or base_val == 0:
            return [float("nan")] * len(df)
        return df[col] / base_val

    df["norm_bw"] = norm("bandwidth_gbs")
    df["norm_avg_latency"] = norm("avg_latency_ns")
    df["norm_evict_count"] = norm("evict_count")

    out_csv = os.path.join(fig_dir, "err_norm.csv")
    df.to_csv(out_csv, index=False)

    metrics = [
        ("norm_bw", f"Bandwidth (norm to {BASELINE})"),
        ("norm_avg_latency", "Average latency"),
        ("norm_evict_count", "Invalidation count"),
    ]
    fig, axes = plt.subplots(1, 3, figsize=(12, 4))
    policies = list(df["policy"])
    for ax, (col, title) in zip(axes, metrics):
        ax.bar(policies, df[col])
        ax.set_title(title)
        ax.set_ylabel("Normalized")
        ax.set_xticklabels(policies, rotation=45, ha="right")
        ax.grid(True, linestyle="--", alpha=0.3)
    fig.tight_layout()
    out_png = os.path.join(fig_dir, "fig13.png")
    fig.savefig(out_png, dpi=150)
    plt.close(fig)

    print(f"Wrote CSV: {out_csv}")
    print(f"Wrote PNG: {out_png}")


if __name__ == "__main__":
    main()
