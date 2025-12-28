#!/usr/bin/env python3
"""
Plot fig14 metrics normalized to len-0: Bandwidth (aggregated across requesters), Average latency (from summary
_avglat.csv), Average wait on invalidation (averaged across requesters from .err).

Usage:
  python scripts/plot_fig14.py [dir]
Default dir: output/fig14
Outputs:
  <dir>/summary/fig14_metrics.csv
  <dir>/summary/fig14_metrics.png (3 bar charts)
"""

import glob
import os
import re
import sys
from typing import Dict, Optional

import matplotlib.pyplot as plt
import pandas as pd

DEFAULT_DIR = "output/fig14"
BASELINE = "len-0"
BW_RE = re.compile(r"Bandwidth \(GB/s\):\s*([0-9.+\-eE]+)")
WAIT_INV_RE = re.compile(r"Average wait for evict \(ns\):\s*([0-9.+\-eE]+)")


def parse_err_agg(path: str) -> Dict[str, Optional[float]]:
    # Aggregate across all requester Aggregate sections
    total_bw = 0.0
    total_wait = 0.0
    wait_cnt = 0
    if not os.path.exists(path):
        return {"bandwidth_gbs": None, "avg_wait_inv_ns": None}
    with open(path, "r") as f:
        lines = [ln.strip() for ln in f]
    in_agg = False
    for ln in lines:
        if "stats:" in ln:
            in_agg = False
        if "Aggregate:" in ln:
            in_agg = True
            continue
        if not in_agg:
            continue
        m_bw = BW_RE.search(ln)
        if m_bw:
            try:
                total_bw += float(m_bw.group(1))
            except ValueError:
                pass
        m_wi = WAIT_INV_RE.search(ln)
        if m_wi:
            try:
                total_wait += float(m_wi.group(1))
                wait_cnt += 1
            except ValueError:
                pass
    avg_wait = (total_wait / wait_cnt) if wait_cnt > 0 else None
    return {"bandwidth_gbs": total_bw if total_bw > 0 else None, "avg_wait_inv_ns": avg_wait}


def read_scalar(summary_dir: str, base: str, suffix: str) -> Optional[float]:
    path = os.path.join(summary_dir, f"{base}_{suffix}.csv")
    if not os.path.exists(path):
        return None
    try:
        with open(path, "r") as f:
            txt = f.read().strip().split()[0]
        return float(txt)
    except Exception:
        return None


def main():
    dir_path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_DIR
    out_dir = os.path.join(dir_path, "summary")
    fig_dir = dir_path
    os.makedirs(out_dir, exist_ok=True)

    # Only base CSVs (len-*) and skip derived *_avglat.csv
    csvs = sorted(
        [p for p in glob.glob(os.path.join(dir_path, "len-*.csv")) if not p.endswith("_avglat.csv") and not p.endswith("_avgwaitinv.csv")]
    )
    rows = []
    for csv in csvs:
        base = os.path.splitext(os.path.basename(csv))[0]
        err = os.path.join(dir_path, base + ".err")
        err_metrics = parse_err_agg(err)
        avg_lat = read_scalar(out_dir, base, "avglat")
        avg_wait_inv = read_scalar(out_dir, base, "avgwaitinv")
        rows.append({
            "len": base,
            "bandwidth_gbs": err_metrics["bandwidth_gbs"],
            "avg_latency_ns": avg_lat,
            "avg_wait_inv_ns": avg_wait_inv,
        })

    if not rows:
        raise SystemExit(f"No CSV/ERR pairs found in {dir_path}")

    df = pd.DataFrame(rows)
    df.sort_values(by="len", inplace=True)

    # Normalize to baseline
    def norm(col: str):
        base_row = df[df["len"] == BASELINE]
        if base_row.empty:
            return [float("nan")] * len(df)
        base_val = base_row.iloc[0][col]
        if base_val is None or pd.isna(base_val) or base_val == 0:
            return [float("nan")] * len(df)
        return df[col] / base_val

    df["norm_bw"] = norm("bandwidth_gbs")
    df["norm_avg_latency"] = norm("avg_latency_ns")
    df["norm_avg_wait_inv"] = norm("avg_wait_inv_ns")
    out_csv = os.path.join(fig_dir, "fig14_metrics.csv")
    df.to_csv(out_csv, index=False)

    metrics = [
        ("norm_bw", "Bandwidth (norm to len-0)"),
        ("norm_avg_latency", "Average latency"),
        ("norm_avg_wait_inv", "Average wait on invalidation"),
    ]
    fig, axes = plt.subplots(1, 3, figsize=(12, 4))
    xlabels = [s.replace("len-", "") for s in df["len"]]
    for ax, (col, title) in zip(axes, metrics):
        ax.bar(xlabels, df[col])
        ax.set_title(title)
        ax.set_ylabel("Normalized")
        ax.set_xticklabels(xlabels, rotation=45, ha="right")
        ax.grid(True, linestyle="--", alpha=0.3)

    fig.tight_layout()
    out_png = os.path.join(fig_dir, "fig14.png")
    fig.savefig(out_png, dpi=150)
    plt.close(fig)

    print(f"Wrote CSV: {out_csv}")
    print(f"Wrote PNG: {out_png}")


if __name__ == "__main__":
    main()
