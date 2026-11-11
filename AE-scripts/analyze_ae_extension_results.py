#!/usr/bin/env python3
"""Summarize AE extension experiment results.

The runner already emits derived fields, but some SnoopFilter averages are
simple means across Snoop instances. This analyzer recomputes weighted sharer
statistics from histogram buckets where possible.
"""

from __future__ import annotations

import csv
from collections import Counter, defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]  # repo root (this script lives in AE-scripts/)
RESULTS_DIR = ROOT / "results" / "ae_extension"
SUMMARY_CSV = RESULTS_DIR / "ae_extension_results.csv"
SUMMARY_MD = RESULTS_DIR / "ae_extension_summary.md"


def legacy_summary_csv() -> Path:
    matches = sorted((ROOT / "results").glob("*_v2/*_v2_results.csv"))
    return matches[0] if matches else SUMMARY_CSV


def f(row: dict[str, str], key: str, default: float = 0.0) -> float:
    try:
        value = row.get(key, "")
        return float(value) if value != "" else default
    except ValueError:
        return default


def weighted_bucket(row: dict[str, str], prefix: str) -> float | None:
    numer = 0.0
    denom = 0.0
    for key, value in row.items():
        if not key.startswith(prefix):
            continue
        try:
            bucket = int(key.removeprefix(prefix))
            count = float(value) if value != "" else 0.0
        except ValueError:
            continue
        numer += bucket * count
        denom += count
    if denom == 0:
        return None
    return numer / denom


def fmt(value: object, digits: int = 3) -> str:
    if value is None or value == "":
        return "-"
    if isinstance(value, float):
        return f"{value:.{digits}f}"
    return str(value)


def load_rows() -> list[dict[str, str]]:
    csv_path = SUMMARY_CSV if SUMMARY_CSV.exists() else legacy_summary_csv()
    if not csv_path.exists():
        raise SystemExit(f"missing {SUMMARY_CSV}")
    with csv_path.open(newline="") as fh:
        rows = list(csv.DictReader(fh))
    for row in rows:
        row["_weighted_evict_sharers"] = fmt(
            weighted_bucket(row, "snoop_sum_evict_sharers_")
        )
        row["_weighted_resident_sharers"] = fmt(
            weighted_bucket(row, "snoop_sum_resident_sharers_")
        )
    return rows


def placement_table(rows: list[dict[str, str]]) -> list[str]:
    lines = [
        "| profile | section | footprint | layout | local | touched | BW GB/s | latency ns |",
        "|---|---:|---:|---|---:|---:|---:|---:|",
    ]
    for row in sorted(
        rows,
        key=lambda r: (
            r["profile"],
            r["section"],
            int(float(r.get("footprint_bytes") or 0)),
            r["layout"],
        ),
    ):
        lines.append(
            "| {profile} | {section} | {footprint} | {layout} | {local} | {touch} | {bw} | {lat} |".format(
                profile=row["profile"],
                section=row["section"],
                footprint=row.get("footprint_bytes") or "-",
                layout=row["layout"],
                local=fmt(f(row, "sanity_mean_local_fraction")),
                touch=fmt(f(row, "sanity_mean_touched_endpoints")),
                bw=fmt(f(row, "host_sum_bw_gbps")),
                lat=fmt(f(row, "host_weighted_avg_latency_ns")),
            )
        )
    return lines


def sf_table(rows: list[dict[str, str]]) -> list[str]:
    lines = [
        "| profile | section | layout | policy | cap | shape | ratio | BW GB/s | latency ns | evict wait ns | inv pkts | conflicts | weighted evict sharers | weighted resident sharers |",
        "|---|---:|---|---|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for row in sorted(
        rows,
        key=lambda r: (
            r["profile"],
            r["section"],
            r["layout"],
            int(float(r.get("snoop_entries") or 0)),
            r.get("sharing_shape", ""),
            float(r.get("shared_ratio") or 0),
            r.get("policy", ""),
        ),
    ):
        lines.append(
            "| {profile} | {section} | {layout} | {policy} | {cap} | {shape} | {ratio} | {bw} | {lat} | {wait} | {inv} | {conf} | {evict} | {resident} |".format(
                profile=row["profile"],
                section=row["section"],
                layout=row["layout"],
                policy=row.get("policy") or "-",
                cap=row.get("snoop_entries") or "-",
                shape=row.get("sharing_shape") or "-",
                ratio=row.get("shared_ratio") or "-",
                bw=fmt(f(row, "host_sum_bw_gbps")),
                lat=fmt(f(row, "host_weighted_avg_latency_ns")),
                wait=fmt(f(row, "host_mean_avg_evict_wait_ns")),
                inv=fmt(f(row, "snoop_sum_total_inv_packets"), 0),
                conf=fmt(f(row, "snoop_sum_total_conflict_count"), 0),
                evict=row.get("_weighted_evict_sharers", "-"),
                resident=row.get("_weighted_resident_sharers", "-"),
            )
        )
    return lines


def policy_deltas(rows: list[dict[str, str]]) -> list[str]:
    grouped: dict[tuple[str, ...], dict[str, dict[str, str]]] = defaultdict(dict)
    for row in rows:
        key = (
            row["profile"],
            row["section"],
            row["layout"],
            row.get("snoop_entries", ""),
            row.get("sharing_shape", ""),
            row.get("shared_ratio", ""),
        )
        grouped[key][row.get("policy", "")] = row

    lines = [
        "| profile | section | layout | cap | shape | ratio | delta BW | delta latency | delta inv pkts | delta conflicts | delta evict wait |",
        "|---|---:|---|---:|---|---:|---:|---:|---:|---:|---:|",
    ]
    for key, pair in sorted(grouped.items()):
        if "LIFO" not in pair or "FS+LIFO" not in pair:
            continue
        lifo = pair["LIFO"]
        fs = pair["FS+LIFO"]
        # Positive latency/inv/conflict/wait deltas mean FS+LIFO reduced the metric.
        lines.append(
            "| {profile} | {section} | {layout} | {cap} | {shape} | {ratio} | {dbw} | {dlat} | {dinv} | {dconf} | {dwait} |".format(
                profile=key[0],
                section=key[1],
                layout=key[2],
                cap=key[3] or "-",
                shape=key[4] or "-",
                ratio=key[5] or "-",
                dbw=fmt(f(fs, "host_sum_bw_gbps") - f(lifo, "host_sum_bw_gbps")),
                dlat=fmt(f(lifo, "host_weighted_avg_latency_ns") - f(fs, "host_weighted_avg_latency_ns")),
                dinv=fmt(f(lifo, "snoop_sum_total_inv_packets") - f(fs, "snoop_sum_total_inv_packets"), 0),
                dconf=fmt(f(lifo, "snoop_sum_total_conflict_count") - f(fs, "snoop_sum_total_conflict_count"), 0),
                dwait=fmt(f(lifo, "host_mean_avg_evict_wait_ns") - f(fs, "host_mean_avg_evict_wait_ns")),
            )
        )
    return lines


def write_summary(rows: list[dict[str, str]]) -> None:
    counts = Counter((row["profile"], row["section"]) for row in rows)
    placement_rows = [row for row in rows if row["section"] in {"1.1", "1.2", "1.3"}]
    sf_rows = [row for row in rows if row["section"] in {"2.1", "2.2", "2.3"}]
    failed = [row for row in rows if row.get("run_status") not in {"ok", "dry_run"}]

    lines = [
        "# AE Extension Experiment Summary",
        "",
        f"Source CSV: `{SUMMARY_CSV.relative_to(ROOT)}`",
        f"Rows: {len(rows)}",
        f"Failed rows: {len(failed)}",
        "",
        "## Row Counts",
        "",
        "| profile | section | rows |",
        "|---|---:|---:|",
    ]
    for (profile, section), count in sorted(counts.items()):
        lines.append(f"| {profile} | {section} | {count} |")

    lines.extend(["", "## Placement Experiments", ""])
    lines.extend(placement_table(placement_rows))

    lines.extend(["", "## Snoop Filter Experiments", ""])
    lines.extend(sf_table(sf_rows))

    lines.extend(
        [
            "",
            "## FS+LIFO Deltas",
            "",
            "Positive latency, invalidation, conflict, and evict-wait deltas mean FS+LIFO reduced that metric relative to LIFO. Positive bandwidth delta means FS+LIFO increased bandwidth.",
            "",
        ]
    )
    lines.extend(policy_deltas(sf_rows))

    if failed:
        lines.extend(["", "## Failed Rows", ""])
        for row in failed:
            lines.append(f"- {row.get('exp_id')}: {row.get('run_status')}")

    SUMMARY_MD.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    rows = load_rows()
    write_summary(rows)
    print(f"Wrote {SUMMARY_MD}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
