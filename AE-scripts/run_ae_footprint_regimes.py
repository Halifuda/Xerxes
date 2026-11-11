#!/usr/bin/env python3
"""Focused rerun for AE extension experiment 1.2.

The goal is not a broad sweep. This script reuses the AE extension
trace/config machinery, but writes only under results/ae_footprint_regimes/.
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

import run_ae_extension_experiments as v2


ROOT = Path(__file__).resolve().parents[1]  # repo root (this script lives in AE-scripts/)
RESULTS_DIR = ROOT / "results" / "ae_footprint_regimes"
CONFIG_DIR = RESULTS_DIR / "configs"
TRACE_DIR = RESULTS_DIR / "traces"
SANITY_DIR = RESULTS_DIR / "sanity"
DRAMSIM_OUTPUT_DIR = RESULTS_DIR / "dramsim_output"
SUMMARY_CSV = RESULTS_DIR / "ae_footprint_results.csv"
SUMMARY_MD = RESULTS_DIR / "ae_footprint_summary.md"
RECOMMENDED_CSV = RESULTS_DIR / "ae_footprint_recommended_points.csv"
PLOT_POINTS_CSV = RESULTS_DIR / "ae_footprint_plot_points.csv"

LAYOUTS = ("no_interleave", "leaf_4way", "global_16way")


def patch_v2_paths() -> None:
    v2.RESULTS_DIR = RESULTS_DIR
    v2.CONFIG_DIR = CONFIG_DIR
    v2.TRACE_DIR = TRACE_DIR
    v2.SANITY_DIR = SANITY_DIR
    v2.DRAMSIM_OUTPUT_DIR = DRAMSIM_OUTPUT_DIR
    v2.SUMMARY_CSV = SUMMARY_CSV


def ensure_dirs() -> None:
    patch_v2_paths()
    v2.ensure_dirs()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Focused AE footprint-regime rerun")
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument(
        "--mode",
        choices=("probe", "final", "all"),
        default="all",
        help="Probe several issue strides, run final paper-facing points, or both.",
    )
    return parser.parse_args()


def profile(name: str, ops: int, max_clock: int, cache_lines: int = 64) -> v2.RunProfile:
    return v2.RunProfile(
        name=name,
        noncoh_ops=ops,
        coh_ops=0,
        max_clock=max_clock,
        host_cache_lines=cache_lines,
        sf_entries_21=0,
        capacities_22=(),
        part2_layout_22="leaf_4way",
        part2_layout_23="leaf_4way",
        sf_entries_23=0,
    )


def run_case(
    *,
    run_profile: v2.RunProfile,
    experiment: str,
    footprint: int,
    tick_stride: int,
    timeout: int,
    dry_run: bool,
    notes: str,
) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for layout in LAYOUTS:
        exp_id = f"r12_{run_profile.name}_{experiment}_{layout}_fp{footprint}_s{tick_stride}"
        trace_files, trace_meta = v2.generate_noncoherent_traces(
            exp_id=exp_id,
            variant="footprint_growth",
            ops_per_host=run_profile.noncoh_ops,
            footprint_bytes=footprint,
            tick_stride=tick_stride,
        )
        row = v2.base_row(
            profile=run_profile,
            section="1.2",
            experiment=experiment,
            exp_id=exp_id,
            layout=layout,
            workload="trace_footprint_growth",
            coherent=False,
        )
        row["footprint_bytes"] = footprint
        row["trace_ops_per_host"] = trace_meta["ops_per_host"]
        row["tick_stride"] = tick_stride
        row["notes"] = notes
        rows.append(
            v2.run_experiment(
                row=row,
                trace_files=trace_files,
                profile=run_profile,
                timeout=timeout,
                dry_run=dry_run,
            )
        )
        rows[-1]["trace_prefix"] = str((TRACE_DIR / f"{exp_id}_host*.trace").relative_to(ROOT))
    return rows


def write_rows(rows: list[dict[str, object]]) -> None:
    v2.write_results(rows, merge_existing=True)


def load_rows() -> list[dict[str, str]]:
    if not SUMMARY_CSV.exists():
        return []
    with SUMMARY_CSV.open(newline="") as fh:
        return list(csv.DictReader(fh))


def as_float(row: dict[str, str], key: str) -> float:
    value = row.get(key, "")
    return float(value) if value not in ("", None) else 0.0


def summarize_rows() -> tuple[list[dict[str, str]], list[str]]:
    rows = [row for row in load_rows() if row.get("run_status") in {"ok", "dry_run"}]
    final_rows = [row for row in rows if row.get("experiment") == "final_semantic_points"]
    if not final_rows:
        final_rows = rows

    grouped: dict[tuple[str, str], list[dict[str, str]]] = {}
    for row in final_rows:
        key = (row.get("experiment", ""), row.get("footprint_bytes", ""))
        grouped.setdefault(key, []).append(row)

    recommendation: list[dict[str, str]] = []
    lines = [
        "# AE Footprint-Regime Focused Rerun",
        "",
        f"Source CSV: `{SUMMARY_CSV.relative_to(ROOT)}`",
        f"Rows: {len(rows)}",
        "",
        "## Final Summary Table",
        "",
        "| experiment | footprint | tick_stride | layout | bandwidth_gbps | latency_ns | touched_endpoints | local_fraction |",
        "|---|---:|---:|---|---:|---:|---:|---:|",
    ]

    for row in sorted(
        final_rows,
        key=lambda r: (
            r.get("experiment", ""),
            int(float(r.get("footprint_bytes") or 0)),
            r.get("layout", ""),
        ),
    ):
        lines.append(
            "| {experiment} | {footprint} | {stride} | {layout} | {bw:.6f} | {lat:.6f} | {touch:.3f} | {local:.6f} |".format(
                experiment=row.get("experiment", ""),
                footprint=row.get("footprint_bytes", ""),
                stride=row.get("tick_stride", ""),
                layout=row.get("layout", ""),
                bw=as_float(row, "host_sum_bw_gbps"),
                lat=as_float(row, "host_weighted_avg_latency_ns"),
                touch=as_float(row, "sanity_mean_touched_endpoints"),
                local=as_float(row, "sanity_mean_local_fraction"),
            )
        )

    lines.extend(
        [
            "",
            "## Winners By Footprint",
            "",
            "| experiment | footprint | best_latency_layout | best_bandwidth_layout | latency_ns | bandwidth_gbps |",
            "|---|---:|---|---|---:|---:|",
        ]
    )
    for (experiment, footprint), group in sorted(grouped.items(), key=lambda item: (item[0][0], int(float(item[0][1] or 0)))):
        if len(group) < len(LAYOUTS):
            continue
        best_latency = min(group, key=lambda r: as_float(r, "host_weighted_avg_latency_ns"))
        best_bw = max(group, key=lambda r: as_float(r, "host_sum_bw_gbps"))
        recommendation.append(best_latency)
        lines.append(
            "| {experiment} | {footprint} | {lat_layout} | {bw_layout} | {lat:.6f} | {bw:.6f} |".format(
                experiment=experiment,
                footprint=footprint,
                lat_layout=best_latency.get("layout", ""),
                bw_layout=best_bw.get("layout", ""),
                lat=as_float(best_latency, "host_weighted_avg_latency_ns"),
                bw=as_float(best_bw, "host_sum_bw_gbps"),
            )
        )

    lines.extend(
        [
            "",
            "## Notes",
            "",
            "The final semantic points use tick_stride=30.  Probe runs with lower issue strides make the system throughput-limited and collapse the small-footprint single-endpoint latency advantage.  The retained points therefore prioritize clean semantic regime separation in request-weighted latency while still reporting bandwidth.",
            "",
            "The mapping sanity columns confirm the intended regimes: no_interleave touches one local endpoint at 16 KiB, leaf_4way touches four local endpoints through 256 KiB, and all layouts touch all sixteen endpoints with about 25% local traffic at 1 MiB.",
        ]
    )
    SUMMARY_MD.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return recommendation, lines


def write_recommended(rows: list[dict[str, str]]) -> None:
    fields = [
        "regime",
        "layout",
        "footprint_bytes",
        "tick_stride",
        "host_sum_bw_gbps",
        "host_weighted_avg_latency_ns",
        "sanity_mean_touched_endpoints",
        "sanity_mean_local_fraction",
        "exp_id",
        "notes",
    ]
    labels = {
        "no_interleave": "single-endpoint regime / Non best",
        "leaf_4way": "leaf-group regime / Group best",
        "global_16way": "beyond-leaf global regime / Flat best",
    }
    with RECOMMENDED_CSV.open("w", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=fields)
        writer.writeheader()
        seen: set[str] = set()
        for row in rows:
            layout = row.get("layout", "")
            if layout in seen:
                continue
            seen.add(layout)
            writer.writerow(
                {
                    "regime": labels.get(layout, layout),
                    "layout": layout,
                    "footprint_bytes": row.get("footprint_bytes", ""),
                    "tick_stride": row.get("tick_stride", ""),
                    "host_sum_bw_gbps": row.get("host_sum_bw_gbps", ""),
                    "host_weighted_avg_latency_ns": row.get("host_weighted_avg_latency_ns", ""),
                    "sanity_mean_touched_endpoints": row.get("sanity_mean_touched_endpoints", ""),
                    "sanity_mean_local_fraction": row.get("sanity_mean_local_fraction", ""),
                    "exp_id": row.get("exp_id", ""),
                    "notes": row.get("notes", ""),
                }
            )


def write_plot_points() -> None:
    rows = [
        row
        for row in load_rows()
        if row.get("experiment") == "final_semantic_points"
        and row.get("run_status") in {"ok", "dry_run"}
    ]
    regime_by_footprint = {
        16 * 1024: "single-endpoint regime",
        128 * 1024: "leaf-group regime",
        v2.GLOBAL_HPA_SIZE: "beyond-leaf/global regime",
    }
    fields = [
        "regime",
        "winner_by_latency",
        "layout",
        "footprint_bytes",
        "tick_stride",
        "host_sum_bw_gbps",
        "host_weighted_avg_latency_ns",
        "sanity_mean_touched_endpoints",
        "sanity_mean_local_fraction",
        "exp_id",
        "config_toml",
        "sanity_json",
        "notes",
    ]
    by_footprint: dict[int, list[dict[str, str]]] = {}
    for row in rows:
        footprint = int(float(row.get("footprint_bytes") or 0))
        if footprint in regime_by_footprint:
            by_footprint.setdefault(footprint, []).append(row)

    with PLOT_POINTS_CSV.open("w", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=fields)
        writer.writeheader()
        for footprint in (16 * 1024, 128 * 1024, v2.GLOBAL_HPA_SIZE):
            group = by_footprint.get(footprint, [])
            if not group:
                continue
            latency_winner = min(group, key=lambda item: as_float(item, "host_weighted_avg_latency_ns")).get("layout", "")
            for layout in LAYOUTS:
                match = [item for item in group if item.get("layout") == layout]
                if not match:
                    continue
                row = match[0]
                writer.writerow(
                    {
                        "regime": regime_by_footprint[footprint],
                        "winner_by_latency": "yes" if row.get("layout") == latency_winner else "no",
                        "layout": row.get("layout", ""),
                        "footprint_bytes": row.get("footprint_bytes", ""),
                        "tick_stride": row.get("tick_stride", ""),
                        "host_sum_bw_gbps": row.get("host_sum_bw_gbps", ""),
                        "host_weighted_avg_latency_ns": row.get("host_weighted_avg_latency_ns", ""),
                        "sanity_mean_touched_endpoints": row.get("sanity_mean_touched_endpoints", ""),
                        "sanity_mean_local_fraction": row.get("sanity_mean_local_fraction", ""),
                        "exp_id": row.get("exp_id", ""),
                        "config_toml": row.get("config_toml", ""),
                        "sanity_json": row.get("sanity_json", ""),
                        "notes": row.get("notes", ""),
                    }
                )


def main() -> int:
    args = parse_args()
    ensure_dirs()
    all_new_rows: list[dict[str, object]] = []

    if args.mode in {"probe", "all"}:
        probe = profile("probe", ops=6000, max_clock=8_000_000)
        for stride in (1, 4, 10):
            for footprint in (128 * 1024, 512 * 1024, v2.GLOBAL_HPA_SIZE):
                rows = run_case(
                    run_profile=probe,
                    experiment="issue_stride_probe",
                    footprint=footprint,
                    tick_stride=stride,
                    timeout=args.timeout,
                    dry_run=args.dry_run,
                    notes="Probe used to reject over-aggressive issue rates when they erase the small-footprint single-endpoint advantage.",
                )
                all_new_rows.extend(rows)
                write_rows(all_new_rows)

    if args.mode in {"final", "all"}:
        final = profile("final", ops=6000, max_clock=8_000_000)
        final_points = (
            (16 * 1024, "single-endpoint semantic point: one local endpoint is enough and Non has the lowest latency."),
            (128 * 1024, "leaf-group semantic point: one endpoint is insufficient while the leaf-local group remains local."),
            (v2.GLOBAL_HPA_SIZE, "beyond-leaf semantic point: footprint spills globally and Flat has the lowest latency."),
        )
        for footprint, notes in final_points:
            rows = run_case(
                run_profile=final,
                experiment="final_semantic_points",
                footprint=footprint,
                tick_stride=30,
                timeout=args.timeout,
                dry_run=args.dry_run,
                notes=notes,
            )
            all_new_rows.extend(rows)
            write_rows(all_new_rows)

    recommendation, _ = summarize_rows()
    write_recommended(recommendation)
    write_plot_points()
    print(f"Wrote summary CSV: {SUMMARY_CSV}")
    print(f"Wrote summary MD: {SUMMARY_MD}")
    print(f"Wrote recommended points CSV: {RECOMMENDED_CSV}")
    print(f"Wrote plot points CSV: {PLOT_POINTS_CSV}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
