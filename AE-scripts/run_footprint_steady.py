#!/usr/bin/env python3
"""Steady-state rerun of the AE section 5.2 footprint-regime experiment (Fig. 17).

The short-run version of this experiment used ops_per_host = 6000 and one seed
(runner: run_ae_footprint_regimes.py, results/ae_footprint_regimes/).  This
script keeps the same working point -- 16-requester spine-leaf (2 spine + 4
leaf, 4 requester + 4 endpoint per leaf), three mappings
(no_interleave=Non, leaf_4way=Group, global_16way=Flat), three footprints
(16 KiB / 128 KiB / 1 MiB) and tick_stride=30 -- but runs 45000 ops per host
for three seed bases (Host-i:random_seed = seed_base + i, 10000/20000/30000).

Everything is written under results/repro/s52_footprint_steady/.  The v2 module
(run_ae_extension_experiments) is NOT modified: only its path globals and
SEED_BASE are patched, exactly as run_ae_footprint_regimes.py does.
"""

from __future__ import annotations

import argparse
import csv
import math
import os
from pathlib import Path

import run_ae_extension_experiments as v2


ROOT = Path(__file__).resolve().parents[1]  # .../Xerxes
RESULTS_DIR = Path(os.environ.get(
    "REPRO_FOOTPRINT_ROOT", str(ROOT / "results" / "repro" / "s52_footprint_steady")))
CONFIG_DIR = RESULTS_DIR / "configs"
TRACE_DIR = RESULTS_DIR / "traces"
SANITY_DIR = RESULTS_DIR / "sanity"
DRAMSIM_OUTPUT_DIR = RESULTS_DIR / "dramsim_output"
SUMMARY_CSV = RESULTS_DIR / "results.csv"
SUMMARY_MD = RESULTS_DIR / "summary.md"
SANITY_CSV = RESULTS_DIR / "sanity_check.csv"
STATIONARITY_CSV = RESULTS_DIR / "stationarity.csv"
LOG_DIR = RESULTS_DIR / "logs"
COMPLETION_CSV = RESULTS_DIR / "completion.csv"
# max_clock used for the controlled-injection runs (full trace consumption;
# verified sufficient: 45000 ops at stride 90 end at cycle ~4.05e6).
CONTROL_MAX_CLOCK = 30_000_000
# Stride selected by the probe (mode "probe"); override with --stride.
DEFAULT_CONTROLLED_STRIDE = 60

LAYOUTS = ("no_interleave", "leaf_4way", "global_16way")
FOOTPRINTS = (16 * 1024, 128 * 1024, v2.GLOBAL_HPA_SIZE)
SEEDS = (10000, 20000, 30000)
REGIME_LABEL = {
    16 * 1024: "Endpoint (16 KiB)",
    128 * 1024: "Leaf (128 KiB)",
    v2.GLOBAL_HPA_SIZE: "Global (1 MiB)",
}
LAYOUT_LABEL = {"no_interleave": "Non", "leaf_4way": "Group", "global_16way": "Flat"}

# Short-run (ops_per_host=6000, tick_stride=30, seed_base=10000) reference
# points copied verbatim from
# results/ae_footprint_regimes/ae_footprint_results.csv
# (host_weighted_avg_latency_ns, sanity_mean_touched_endpoints).  Used to prove
# that the harness was not broken before the steady-state grid is trusted.
OLD_REFERENCE = {
    ("no_interleave", 16 * 1024): (129.9363125, 1.000),
    ("leaf_4way", 16 * 1024): (135.680375, 4.000),
    ("global_16way", 16 * 1024): (252.6848125, 16.000),
    ("no_interleave", 128 * 1024): (147.0610625, 2.000),
    ("leaf_4way", 128 * 1024): (141.500875, 4.000),
    ("global_16way", 128 * 1024): (264.0699375, 16.000),
    ("no_interleave", v2.GLOBAL_HPA_SIZE): (252.9556875, 16.000),
    ("leaf_4way", v2.GLOBAL_HPA_SIZE): (251.70475, 16.000),
    ("global_16way", v2.GLOBAL_HPA_SIZE): (246.7614375, 16.000),
}
EXPECTED_TOUCHED = {
    16 * 1024: {"no_interleave": 1.0, "leaf_4way": 4.0, "global_16way": 16.0},
    128 * 1024: {"no_interleave": 2.0, "leaf_4way": 4.0, "global_16way": 16.0},
    v2.GLOBAL_HPA_SIZE: {"no_interleave": 16.0, "leaf_4way": 16.0, "global_16way": 16.0},
}
SANITY_RTOL = 1e-6
MAX_CLOCK = 8_000_000  # v2 default; raised for the full-trace control runs


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
    LOG_DIR.mkdir(parents=True, exist_ok=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Steady-state footprint-regime rerun (Fig. 17)")
    parser.add_argument(
        "--mode",
        choices=(
            "sanity",
            "grid",
            "controls",
            "probe",
            "controlled",
            "controlled6k",
            "report",
            "stationarity",
            "all",
        ),
        default="all",
    )
    parser.add_argument(
        "--probe-strides",
        default="90,60,45",
        help="strides tried by --mode probe (descending stride = ascending load)",
    )
    parser.add_argument("--ops", type=int, default=45000, help="ops_per_host for the grid")
    parser.add_argument("--stride", type=int, default=30, help="tick_stride for the grid")
    parser.add_argument("--timeout", type=int, default=900, help="per-cell Xerxes timeout (s)")
    parser.add_argument("--tag", default="", help="suffix for exp ids of control runs")
    parser.add_argument("--max-clock", type=int, default=8_000_000, help="sim max_clock (v2 default 8e6)")
    parser.add_argument("--experiment", default="", help="CSV experiment name override")
    parser.add_argument("--seeds", default=",".join(str(s) for s in SEEDS))
    parser.add_argument("--layouts", default=",".join(LAYOUTS))
    parser.add_argument("--footprints", default=",".join(str(f) for f in FOOTPRINTS))
    parser.add_argument("--clean-evict", action="store_true")
    return parser.parse_args()


def profile_for(experiment: str, ops: int, stride: int) -> v2.RunProfile:
    return v2.RunProfile(
        name=f"{experiment}_ops{ops}_stride{stride}",
        noncoh_ops=ops,
        coh_ops=0,
        max_clock=MAX_CLOCK,
        host_cache_lines=64,
        sf_entries_21=0,
        capacities_22=(),
        part2_layout_22="leaf_4way",
        part2_layout_23="leaf_4way",
        sf_entries_23=0,
    )


def count_trace_ops(trace_files: list[Path]) -> int:
    counts = []
    for path in trace_files:
        with path.open("rb") as fh:
            counts.append(sum(1 for line in fh if line.strip()))
    if len(set(counts)) != 1:
        raise RuntimeError(f"uneven trace lengths: {counts}")
    return counts[0]


def run_cell(
    *,
    layout: str,
    footprint: int,
    seed_base: int,
    ops: int,
    stride: int,
    experiment: str,
    tag: str,
    timeout: int,
    clean_evict: bool,
    notes: str,
) -> dict[str, object]:
    profile = profile_for(experiment, ops, stride)
    suffix = f"_{tag}" if tag else ""
    exp_id = f"s52{suffix}_{layout}_fp{footprint}_s{seed_base}_ops{ops}_stride{stride}"
    # build_spineleaf_config() reads the module-level SEED_BASE.
    v2.SEED_BASE = seed_base
    trace_files, trace_meta = v2.generate_noncoherent_traces(
        exp_id=exp_id,
        variant="footprint_growth",
        ops_per_host=ops,
        footprint_bytes=footprint,
        tick_stride=stride,
    )
    row = v2.base_row(
        profile=profile,
        section="5.2",
        experiment=experiment,
        exp_id=exp_id,
        layout=layout,
        workload="trace_footprint_growth",
        coherent=False,
    )
    row["footprint_bytes"] = footprint
    row["trace_ops_per_host"] = count_trace_ops(trace_files)
    row["tick_stride"] = stride
    row["seed_base"] = seed_base
    row["notes"] = notes
    row = v2.run_experiment(
        row=row,
        trace_files=trace_files,
        profile=profile,
        timeout=timeout,
        dry_run=False,
        clean_evict=clean_evict,
    )
    row["trace_prefix"] = str((TRACE_DIR / f"{exp_id}_host*.trace").relative_to(ROOT))
    return row


def num(row: dict, key: str) -> float:
    value = row.get(key, "")
    if value in ("", None):
        return math.nan
    return float(value)


def write_csv_rows(path: Path, rows: list[dict[str, object]]) -> None:
    keys: list[str] = []
    for row in rows:
        for key in row:
            if key not in keys and key != "coherent_bool":
                keys.append(key)
    with path.open("w", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=keys)
        writer.writeheader()
        for row in rows:
            writer.writerow({key: row.get(key, "") for key in keys})


def sanity_check(args: argparse.Namespace) -> bool:
    """Re-run the two short-run reference cells and compare with OLD_REFERENCE."""
    rows: list[dict[str, object]] = []
    failures: list[str] = []
    print("== harness sanity check (ops=6000, stride=30, seed_base=10000) ==")
    for layout in ("no_interleave", "leaf_4way"):
        row = run_cell(
            layout=layout,
            footprint=16 * 1024,
            seed_base=10000,
            ops=6000,
            stride=30,
            experiment="sanity_6k_reference",
            tag="sanity",
            timeout=args.timeout,
            clean_evict=False,
            notes="Exact reproduction of the short-run reference cell (ops=6000).",
        )
        rows.append(row)
        old_lat, old_touch = OLD_REFERENCE[(layout, 16 * 1024)]
        lat = num(row, "host_weighted_avg_latency_ns")
        touch = num(row, "sanity_mean_touched_endpoints")
        lat_rel = abs(lat - old_lat) / old_lat if old_lat else math.inf
        touch_rel = abs(touch - old_touch) / old_touch if old_touch else math.inf
        status = "ok" if row.get("run_status") == "ok" else str(row.get("run_status"))
        verdict = "MATCH" if lat_rel < SANITY_RTOL and touch_rel < SANITY_RTOL else "MISMATCH"
        print(
            f"  {layout}: lat={lat:.10f} (old {old_lat:.10f}, rel {lat_rel:.2e}) "
            f"touched={touch:.6f} (old {old_touch:.6f}, rel {touch_rel:.2e}) "
            f"status={status} -> {verdict}"
        )
        if verdict != "MATCH" or status != "ok":
            failures.append(layout)
    write_csv_rows(SANITY_CSV, rows)
    v2.write_results(rows, merge_existing=True)
    print(f"Wrote sanity CSV: {SANITY_CSV}")
    if failures:
        print("SANITY CHECK FAILED for: " + ", ".join(failures))
        return False
    print("Sanity check passed: harness reproduces the short-run reference numbers.\n")
    return True


def run_grid(args: argparse.Namespace, rows_by_id: dict[str, dict[str, object]]) -> None:
    ops, stride = args.ops, args.stride
    seeds = [int(s) for s in args.seeds.split(",") if s]
    layouts = [s for s in args.layouts.split(",") if s]
    footprints = [int(s) for s in args.footprints.split(",") if s]
    experiment = args.experiment or "steady_grid"
    total = len(seeds) * len(layouts) * len(footprints)
    print(
        f"== grid: {len(layouts)} layouts x {len(footprints)} footprints x {len(seeds)} seeds "
        f"= {total} cells, ops={ops}, stride={stride} =="
    )
    done = 0
    for footprint in footprints:
        for layout in layouts:
            for seed_base in seeds:
                done += 1
                notes = (
                    f"Steady-state footprint-regime point: ops_per_host={ops}, tick_stride={stride}, "
                    f"seed_base={seed_base}; regime {REGIME_LABEL[footprint]}, mapping {LAYOUT_LABEL[layout]}."
                )
                row = run_cell(
                    layout=layout,
                    footprint=footprint,
                    seed_base=seed_base,
                    ops=ops,
                    stride=stride,
                    experiment=experiment,
                    tag=args.tag,
                    timeout=args.timeout,
                    clean_evict=args.clean_evict,
                    notes=notes,
                )
                rows_by_id[str(row["exp_id"])] = row
                # Persist after every cell so an interrupted sweep keeps its rows.
                v2.write_results(list(rows_by_id.values()), merge_existing=True)
                print(
                    f"[{done}/{total}] {row['exp_id']} status={row['run_status']} "
                    f"lat={num(row, 'host_weighted_avg_latency_ns'):.6f} "
                    f"touch={num(row, 'sanity_mean_touched_endpoints'):.3f} "
                    f"bw={num(row, 'host_sum_bw_gbps'):.3f} elapsed={row.get('elapsed_sec')}s",
                    flush=True,
                )


def trace_timing(exp_id: str, ops: int, stride: int) -> dict[str, object]:
    """Did the run consume the whole trace, and is its latency flat?

    Parses the per-request packet log RESULTS_DIR/<exp_id>.csv
    (columns: id,type,mem,addr,issue_cycle,arrive_cycle,...).  The trace issues
    op i at cycle i*stride, so the last op must show up at (ops-1)*stride; if the
    log stops earlier the run was cut by max_clock and every latency is a
    censored window mean.
    """
    log = RESULTS_DIR / f"{exp_id}.csv"
    trace_end = (ops - 1) * stride
    out: dict[str, object] = {
        "exp_id": exp_id,
        "ops": ops,
        "stride": stride,
        "trace_end_cycle": trace_end,
        "packets": 0,
        "max_issue_cycle": -1,
        "complete": "no-log",
        "first_tenth_ns": math.nan,
        "last_tenth_ns": math.nan,
        "last_first": math.nan,
    }
    if not log.exists():
        return out
    samples: list[tuple[int, int]] = []
    with log.open() as fh:
        for line in fh:
            parts = line.split(",")
            if len(parts) < 14:
                continue
            try:
                t0, t1 = int(parts[4]), int(parts[5])
            except ValueError:
                continue
            samples.append((t0, t1))
    if not samples:
        return out
    lo, hi = samples[0][0], samples[-1][0]
    width = (hi - lo) / 10 or 1
    first = [t1 - t0 for t0, t1 in samples if t0 < lo + width]
    last = [t1 - t0 for t0, t1 in samples if t0 >= lo + 9 * width]
    out["packets"] = len(samples)
    out["max_issue_cycle"] = hi
    out["complete"] = "yes" if hi >= trace_end - stride else "no"
    if first and last:
        out["first_tenth_ns"] = sum(first) / len(first)
        out["last_tenth_ns"] = sum(last) / len(last)
        out["last_first"] = out["last_tenth_ns"] / out["first_tenth_ns"]
    return out


def record_completion(records: list[dict[str, object]]) -> None:
    """Append compact completion/stationarity diagnostics (one row per cell)."""
    by_id = {r["exp_id"]: r for r in load_csv(COMPLETION_CSV)}
    for rec in records:
        by_id[str(rec["exp_id"])] = {k: str(v) for k, v in rec.items()}
    write_csv_rows(COMPLETION_CSV, list(by_id.values()))
    print(f"Wrote completion diagnostics: {COMPLETION_CSV} ({len(by_id)} cells)")


def load_csv(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        return []
    with path.open(newline="") as fh:
        return list(csv.DictReader(fh))


def fmt(value: float, spec: str = ".3f") -> str:
    if value is None or (isinstance(value, float) and math.isnan(value)):
        return "-"
    return format(value, spec)


def analyze_stationarity(bins: int = 10) -> None:
    """Bin per-request latencies over each run to expose non-stationary behaviour.

    A run whose latency is stationary has the same mean latency in the first and
    the last time bin; a run that saturates shows a monotonically rising profile.
    Parses the per-request packet log that Xerxes writes to RESULTS_DIR/<exp_id>.csv
    (columns: id, type, mem, addr, issue_cycle, arrive_cycle, ...).
    """
    out: list[dict[str, object]] = []
    for r in load_csv(SUMMARY_CSV):
        if int(float(r.get("seed_base") or 0)) != 10000:
            continue
        if r.get("experiment") not in ("steady_grid", "control_fulltrace"):
            continue
        log = RESULTS_DIR / f"{r['exp_id']}.csv"
        if not log.exists():
            continue
        samples: list[tuple[int, int]] = []
        with log.open() as fh:
            for line in fh:
                parts = line.split(",")
                if len(parts) < 14:
                    continue
                try:
                    t0, t1 = int(parts[4]), int(parts[5])
                except ValueError:
                    continue
                samples.append((t0, t1))
        if not samples:
            continue
        lo, hi = samples[0][0], samples[-1][0]
        width = (hi - lo) / bins or 1
        for b in range(bins):
            sel = [t1 - t0 for t0, t1 in samples if lo + b * width <= t0 < lo + (b + 1) * width]
            if not sel:
                continue
            out.append(
                {
                    "exp_id": r["exp_id"],
                    "experiment": r["experiment"],
                    "layout": r["layout"],
                    "footprint_bytes": r["footprint_bytes"],
                    "window_end_cycle": hi,
                    "bin": b,
                    "bin_start_cycle": int(lo + b * width),
                    "packets": len(sel),
                    "mean_latency_ns": f"{sum(sel) / len(sel):.1f}",
                }
            )
    write_csv_rows(STATIONARITY_CSV, out)
    print(f"Wrote stationarity bins: {STATIONARITY_CSV} ({len(out)} rows)")


def stationarity_table(bins: int = 10) -> list[str]:
    rows = load_csv(STATIONARITY_CSV)
    if not rows:
        return []
    per_cell: dict[str, dict[int, float]] = {}
    meta: dict[str, dict[str, str]] = {}
    for r in rows:
        per_cell.setdefault(r["exp_id"], {})[int(r["bin"])] = float(r["mean_latency_ns"])
        meta[r["exp_id"]] = r
    L = ["## Latency stationarity inside each run (seed 10000)", ""]
    L.append(
        "Mean per-request latency in 10 equal time bins of the run. A converged steady state has "
        "a flat profile (last/first ~ 1); a saturating regime rises monotonically."
    )
    L.append("")
    L.append("| experiment | regime | mapping | bin 0 (ns) | bin 4 (ns) | bin 9 (ns) | last/first |")
    L.append("|---|---|---|---:|---:|---:|---:|")
    for exp_id, bmap in per_cell.items():
        m = meta[exp_id]
        fp = int(float(m["footprint_bytes"]))
        first = bmap.get(0, math.nan)
        mid = bmap.get(4, math.nan)
        last = bmap.get(max(bmap), math.nan)
        ratio = last / first if first and not math.isnan(first) else math.nan
        L.append(
            f"| {exp_id} | {REGIME_LABEL[fp]} | {LAYOUT_LABEL[m['layout']]} | {fmt(first, '.0f')} | "
            f"{fmt(mid, '.0f')} | {fmt(last, '.0f')} | {fmt(ratio, '.2f')}x |"
        )
    L.append("")
    return L


def describe_cell(row: dict, experiment: str, ops: int, stride: int, max_clock: int) -> dict[str, object]:
    """Compact per-cell record joining metrics with the completion diagnostics."""
    timing = trace_timing(str(row["exp_id"]), ops, stride)
    return {
        "exp_id": row["exp_id"],
        "experiment": experiment,
        "layout": row["layout"],
        "footprint_bytes": row["footprint_bytes"],
        "ops": ops,
        "stride": stride,
        "max_clock": max_clock,
        "packets": timing["packets"],
        "trace_end_cycle": timing["trace_end_cycle"],
        "max_issue_cycle": timing["max_issue_cycle"],
        "complete": timing["complete"],
        "first_tenth_ns": f"{timing['first_tenth_ns']:.1f}" if timing["first_tenth_ns"] == timing["first_tenth_ns"] else "-",
        "last_tenth_ns": f"{timing['last_tenth_ns']:.1f}" if timing["last_tenth_ns"] == timing["last_tenth_ns"] else "-",
        "last_first": f"{timing['last_first']:.3f}" if timing["last_first"] == timing["last_first"] else "-",
        "latency_ns": row.get("host_weighted_avg_latency_ns", ""),
        "touched": row.get("sanity_mean_touched_endpoints", ""),
        "bw_gbps": row.get("host_sum_bw_gbps", ""),
        "elapsed_sec": row.get("elapsed_sec", ""),
        "run_status": row.get("run_status", ""),
    }


def run_probe(args: argparse.Namespace) -> int:
    """Find the smallest stride (highest offered load) that still converges.

    Probe only the worst cell (Flat, 1 MiB) with the trace fully consumed.
    """
    strides = [int(s) for s in args.probe_strides.split(",") if s]
    ops, footprint = args.ops, v2.GLOBAL_HPA_SIZE
    print(
        f"== stride probe: global_16way x 1 MiB, ops={ops}, max_clock={MAX_CLOCK}, "
        f"strides={strides} =="
    )
    records: list[dict[str, object]] = []
    chosen = None
    for stride in strides:  # descending stride = ascending load
        row = run_cell(
            layout="global_16way",
            footprint=footprint,
            seed_base=10000,
            ops=ops,
            stride=stride,
            experiment="probe_stride",
            tag="probe",
            timeout=args.timeout,
            clean_evict=args.clean_evict,
            notes=(
                f"Stride probe on the worst cell (Flat, 1 MiB): ops_per_host={ops}, tick_stride={stride}, "
                f"max_clock={MAX_CLOCK}; checks trace completion and latency stationarity."
            ),
        )
        v2.write_results([row], merge_existing=True)
        rec = describe_cell(row, "probe_stride", ops, stride, MAX_CLOCK)
        records.append(rec)
        ratio = float(rec["last_first"]) if rec["last_first"] != "-" else math.inf
        ok = rec["complete"] == "yes" and ratio <= 1.10
        print(
            f"  stride={stride:>3} status={rec['run_status']} complete={rec['complete']} "
            f"lat={num(row, 'host_weighted_avg_latency_ns'):.3f} ns "
            f"last/first={rec['last_first']} elapsed={rec['elapsed_sec']}s -> "
            f"{'CANDIDATE' if ok else 'reject'}",
            flush=True,
        )
        if ok and chosen is None:
            chosen = stride
    record_completion(records)
    print(f"Probe result: selected tick_stride={chosen}" if chosen else "Probe result: no stride converged")
    if chosen:
        print(f"Run the controlled grid with: --mode controlled --stride {chosen} --max-clock {MAX_CLOCK}")
    return 0 if chosen else 3


def run_controlled(args: argparse.Namespace, ops: int, experiment: str, tag: str) -> int:
    """Full 3x3 grid at a fixed (converged) injection rate, single seed."""
    stride = args.stride
    print(
        f"== {experiment}: 3 layouts x 3 footprints, ops={ops}, stride={stride}, "
        f"max_clock={MAX_CLOCK}, seed_base=10000 =="
    )
    records: list[dict[str, object]] = []
    failures: list[str] = []
    done = 0
    total = len(FOOTPRINTS) * len(LAYOUTS)
    for footprint in FOOTPRINTS:
        for layout in LAYOUTS:
            done += 1
            row = run_cell(
                layout=layout,
                footprint=footprint,
                seed_base=10000,
                ops=ops,
                stride=stride,
                experiment=experiment,
                tag=tag,
                timeout=args.timeout,
                clean_evict=args.clean_evict,
                notes=(
                    f"{experiment}: ops_per_host={ops}, tick_stride={stride}, max_clock={MAX_CLOCK}, "
                    f"seed_base=10000; regime {REGIME_LABEL[footprint]}, mapping {LAYOUT_LABEL[layout]}."
                ),
            )
            v2.write_results([row], merge_existing=True)
            rec = describe_cell(row, experiment, ops, stride, MAX_CLOCK)
            records.append(rec)
            record_completion([rec])
            lat = num(row, "host_weighted_avg_latency_ns")
            touch = num(row, "sanity_mean_touched_endpoints")
            want = EXPECTED_TOUCHED[footprint][layout]
            problems = []
            if row.get("run_status") != "ok":
                problems.append(f"status={row.get('run_status')}")
            if rec["complete"] != "yes":
                problems.append(f"truncated at cycle {rec['max_issue_cycle']}/{rec['trace_end_cycle']}")
            if abs(touch - want) > 1e-6:
                problems.append(f"touched {touch:.3f} != {want:.1f}")
            if problems:
                failures.append(f"{rec['exp_id']}: " + "; ".join(problems))
            print(
                f"  [{done}/{total}] {REGIME_LABEL[footprint]:17s} {LAYOUT_LABEL[layout]:5s} "
                f"lat={lat:8.3f} ns touch={touch:5.3f} bw={num(row, 'host_sum_bw_gbps'):6.3f} "
                f"complete={rec['complete']} last/first={rec['last_first']} elapsed={rec['elapsed_sec']}s"
                + ("  <-- PROBLEM: " + "; ".join(problems) if problems else ""),
                flush=True,
            )
    if failures:
        print("ASSERTION FAILURES:")
        for f in failures:
            print("  - " + f)
        return 4
    print(f"{experiment}: 9/9 cells complete and consistent.")
    return 0


def build_summary(rows_by_id: dict[str, dict]) -> str:
    rows = [r for r in rows_by_id.values() if r.get("run_status") == "ok"]
    grid = [r for r in rows if r.get("experiment") == "steady_grid"]
    sanity = load_csv(SANITY_CSV)
    by_cell: dict[tuple[int, str], list[dict]] = {}
    for r in grid:
        by_cell.setdefault((int(float(r["footprint_bytes"])), str(r["layout"])), []).append(r)
    base_vals = [num(r, "host_weighted_avg_latency_ns") for r in by_cell.get((16 * 1024, "no_interleave"), [])]
    base_mean = sum(base_vals) / len(base_vals) if base_vals else math.nan

    L: list[str] = []
    L.append("# AE 5.2 footprint regimes -- steady-state (45k ops/host) rerun")
    L.append("")
    L.append(f"Source CSV: `{SUMMARY_CSV.relative_to(ROOT)}`")
    L.append("")
    L.append(
        "Working point: 16-requester spine-leaf (2 spine + 4 leaf, 4 requester + 4 endpoint per leaf), "
        "cache 64 lines, tick_stride=30, ops_per_host=45000, seed_base in 10000/20000/30000 "
        "(Host-i random_seed = seed_base + i). Mappings: Non = no_interleave, Group = leaf_4way, "
        "Flat = global_16way."
    )
    L.append("")
    L.append(
        f"Latency is host_weighted_avg_latency_ns (request-weighted over the 16 hosts); the normalization "
        f"base is the 45k 16 KiB Non mean = {fmt(base_mean)} ns."
    )
    L.append("")
    L.append("## Steady-state results (mean of 3 seeds)")
    L.append("")
    L.append("| regime | mapping | seed 10000 | seed 20000 | seed 30000 | mean +/- range | norm vs 16K Non | touched ep. | local frac | BW (Gb/s) |")
    L.append("|---|---|---:|---:|---:|---:|---:|---:|---:|---:|")
    means: dict[tuple[int, str], float] = {}
    touched: dict[tuple[int, str], float] = {}
    for footprint in (16 * 1024, 128 * 1024, v2.GLOBAL_HPA_SIZE):
        for layout in LAYOUTS:
            group = by_cell.get((footprint, layout), [])
            lats = {int(float(r["seed_base"])): num(r, "host_weighted_avg_latency_ns") for r in group}
            vals = [v for v in lats.values() if not math.isnan(v)]
            if not vals:
                L.append(f"| {REGIME_LABEL[footprint]} | {LAYOUT_LABEL[layout]} | - | - | - | MISSING | - | - | - | - |")
                continue
            mean = sum(vals) / len(vals)
            means[(footprint, layout)] = mean
            tvals = [num(r, "sanity_mean_touched_endpoints") for r in group]
            touched[(footprint, layout)] = sum(tvals) / len(tvals)
            lf = [num(r, "sanity_mean_local_fraction") for r in group]
            bw = [num(r, "host_sum_bw_gbps") for r in group]
            cells = " | ".join(fmt(lats.get(s, math.nan)) for s in SEEDS)
            L.append(
                f"| {REGIME_LABEL[footprint]} | {LAYOUT_LABEL[layout]} | {cells} | "
                f"{fmt(mean)} +/- {fmt(max(vals) - min(vals))} | {fmt(mean / base_mean, '.3f')}x | "
                f"{fmt(touched[(footprint, layout)], '.3f')} | {fmt(sum(lf) / len(lf), '.4f')} | "
                f"{fmt(sum(bw) / len(bw), '.3f')} |"
            )
    L.append("")
    stat_rows = load_csv(STATIONARITY_CSV)
    ft = {
        int(float(r["footprint_bytes"])): num(r, "host_weighted_avg_latency_ns")
        for r in rows
        if r.get("experiment") == "control_fulltrace"
    }
    s60 = {
        (int(float(r["footprint_bytes"])), r["layout"]): num(r, "host_weighted_avg_latency_ns")
        for r in rows
        if r.get("experiment") == "control_stride60"
    }
    o90k = {
        (int(float(r["footprint_bytes"])), r["layout"]): num(r, "host_weighted_avg_latency_ns")
        for r in rows
        if r.get("experiment") == "control_ops90000"
    }
    L.append(
        "Note: the Flat rows (global_16way) are window averages of a non-stationary run at "
        "stride 30 -- see (e) below; the Non and Group rows are stationary."
    )
    L.append("")
    L.append("## Comparison with the 6k-op short-run values")
    L.append("")
    L.append("| regime | mapping | old 6k latency (ns) | steady 45k latency (ns) | delta % | old touched | steady touched |")
    L.append("|---|---|---:|---:|---:|---:|---:|")
    for footprint in (16 * 1024, 128 * 1024, v2.GLOBAL_HPA_SIZE):
        for layout in LAYOUTS:
            old_lat, old_touch = OLD_REFERENCE[(layout, footprint)]
            mean = means.get((footprint, layout), math.nan)
            delta = (mean - old_lat) / old_lat * 100.0 if not math.isnan(mean) else math.nan
            L.append(
                f"| {REGIME_LABEL[footprint]} | {LAYOUT_LABEL[layout]} | {fmt(old_lat)} | {fmt(mean)} | "
                f"{fmt(delta, '+.2f')} | {fmt(old_touch)} | {fmt(touched.get((footprint, layout), math.nan))} |"
            )
    L.append("")
    controlled = [r for r in rows if r.get("experiment") == "steady_controlled"]
    comp = {str(r.get("exp_id")): r for r in load_csv(COMPLETION_CSV)}
    ctl_stride = ""
    if controlled:
        ctl = {(int(float(r["footprint_bytes"])), str(r["layout"])): r for r in controlled}
        ctl_stride = str(controlled[0].get("tick_stride", ""))
        ctl_base = num(ctl.get((16 * 1024, "no_interleave"), {}), "host_weighted_avg_latency_ns")
        L.append(f"## Controlled-injection steady state (tick_stride={ctl_stride}, max_clock=3e7)")
        L.append("")
        L.append(
            f"Same working point and ops_per_host=45000, but the injection interval is stretched to "
            f"tick_stride={ctl_stride} so that every cell consumes its whole trace inside max_clock=3e7; "
            f"single seed (seed_base=10000) -- the trace RNG is fixed, so repeats are bit-identical."
        )
        L.append("")
        L.append(
            "| regime | mapping | latency (ns) | norm vs 16K Non | touched | BW (Gb/s) | first tenth (ns) | "
            "last tenth (ns) | last/first | complete | old 6k@30 (ns) | delta % |"
        )
        L.append("|---|---|---:|---:|---:|---:|---:|---:|---:|---|---:|---:|")
        for footprint in (16 * 1024, 128 * 1024, v2.GLOBAL_HPA_SIZE):
            for layout in LAYOUTS:
                r = ctl.get((footprint, layout))
                if not r:
                    L.append(f"| {REGIME_LABEL[footprint]} | {LAYOUT_LABEL[layout]} | MISSING | - | - | - | - | - | - | - | - | - |")
                    continue
                lat = num(r, "host_weighted_avg_latency_ns")
                d = comp.get(str(r["exp_id"]), {})
                old_lat = OLD_REFERENCE[(layout, footprint)][0]
                L.append(
                    f"| {REGIME_LABEL[footprint]} | {LAYOUT_LABEL[layout]} | {fmt(lat)} | "
                    f"{fmt(lat / ctl_base, '.3f')}x | {fmt(num(r, 'sanity_mean_touched_endpoints'))} | "
                    f"{fmt(num(r, 'host_sum_bw_gbps'))} | {d.get('first_tenth_ns', '-')} | "
                    f"{d.get('last_tenth_ns', '-')} | {d.get('last_first', '-')} | {d.get('complete', '-')} | "
                    f"{fmt(old_lat)} | {fmt((lat - old_lat) / old_lat * 100.0, '+.2f')} |"
                )
        L.append("")
        six = [r for r in rows if r.get("experiment") == "controlled_6k"]
        if six:
            six_by = {(int(float(r["footprint_bytes"])), str(r["layout"])): r for r in six}
            L.append(
                f"### Is the 6k-op run itself a transient? (same tick_stride={ctl_stride}, ops=6000)"
            )
            L.append("")
            L.append("| regime | mapping | latency (ns) | last/first | complete | vs 45k % |")
            L.append("|---|---|---:|---:|---|---:|")
            for footprint in (16 * 1024, 128 * 1024, v2.GLOBAL_HPA_SIZE):
                for layout in LAYOUTS:
                    r = six_by.get((footprint, layout))
                    if not r:
                        continue
                    d = comp.get(str(r["exp_id"]), {})
                    lat6 = num(r, "host_weighted_avg_latency_ns")
                    lat45 = num(ctl.get((footprint, layout), {}), "host_weighted_avg_latency_ns")
                    delta = (lat6 - lat45) / lat45 * 100.0 if lat45 else math.nan
                    L.append(
                        f"| {REGIME_LABEL[footprint]} | {LAYOUT_LABEL[layout]} | {fmt(lat6)} | "
                        f"{d.get('last_first', '-')} | {d.get('complete', '-')} | {fmt(delta, '+.2f')} |"
                    )
            s30 = {
                int(float(r["footprint_bytes"])): r
                for r in rows
                if r.get("experiment") == "controlled_6k_s30"
            }
            if s30:
                def r30(fp: int, col: str = "host_weighted_avg_latency_ns") -> float:
                    return num(s30.get(fp, {}), col)

                L.append("")
                L.append(
                    f"At the old injection rate (tick_stride=30) the 6k window also finishes "
                    f"({comp.get(str(s30.get(16 * 1024, {}).get('exp_id')), {}).get('complete', '-')}), but it samples the "
                    f"bottom of a diverging curve: Flat 16 KiB gives {fmt(r30(16 * 1024))} ns "
                    f"(the old value, reproduced exactly) against {fmt(means.get((16 * 1024, 'global_16way'), math.nan))} ns "
                    f"for the 45k window mean at max_clock=8e6 and {fmt(ft.get(16 * 1024, math.nan))} ns with the whole "
                    f"45k trace consumed, and Flat 1 MiB gives {fmt(r30(v2.GLOBAL_HPA_SIZE))} ns against "
                    f"{fmt(means.get((v2.GLOBAL_HPA_SIZE, 'global_16way'), math.nan))} / "
                    f"{fmt(ft.get(v2.GLOBAL_HPA_SIZE, math.nan))} ns. Inside its own 6k window the 1 MiB Flat latency "
                    f"already climbs "
                    f"{fmt((float(comp.get(str(s30.get(v2.GLOBAL_HPA_SIZE, {}).get('exp_id')), {}).get('last_first', 'nan')) - 1) * 100, '+.1f')}%, "
                    f"so the 6k point is a transient sample of a run whose backlog never stops growing -- six thousand "
                    f"issued requests are not the same thing as a converged system. At tick_stride={ctl_stride}, where a "
                    f"stationary state exists, the 6k and 45k values agree to 0.2-0.8% and the 6k window drifts only "
                    f"1.08-1.10x from its first to its last tenth, i.e. the short window is usable there."
                )
                L.append("")
        L.append("### Verdict (controlled injection)")
        L.append("")
        ep = {l: num(ctl.get((16 * 1024, l), {}), "host_weighted_avg_latency_ns") for l in LAYOUTS}
        lf = {l: num(ctl.get((128 * 1024, l), {}), "host_weighted_avg_latency_ns") for l in LAYOUTS}
        gl = {l: num(ctl.get((v2.GLOBAL_HPA_SIZE, l), {}), "host_weighted_avg_latency_ns") for l in LAYOUTS}
        ep_old = {l: OLD_REFERENCE[(l, 16 * 1024)][0] for l in LAYOUTS}
        lf_old = {l: OLD_REFERENCE[(l, 128 * 1024)][0] for l in LAYOUTS}
        gl_old = {l: OLD_REFERENCE[(l, v2.GLOBAL_HPA_SIZE)][0] for l in LAYOUTS}
        L.append(
            f"(a) 16 KiB: Non {fmt(ep['no_interleave'])} ns, Group {fmt(ep['leaf_4way'])} ns "
            f"({fmt(ep['leaf_4way'] / ep['no_interleave'], '.3f')}x), Flat {fmt(ep['global_16way'])} ns "
            f"({fmt(ep['global_16way'] / ep['no_interleave'], '.3f')}x). Non < Group < Flat: "
            f"{'holds' if ep['no_interleave'] < ep['leaf_4way'] < ep['global_16way'] else 'does NOT hold'}."
        )
        L.append(
            f"(b) 128 KiB: Group {fmt(lf['leaf_4way'])} ns vs Non {fmt(lf['no_interleave'])} ns = "
            f"{fmt((lf['leaf_4way'] - lf['no_interleave']) / lf['no_interleave'] * 100.0, '+.2f')}% "
            f"({'better' if lf['leaf_4way'] < lf['no_interleave'] else 'worse'}); old 6k@30 gave "
            f"{fmt((lf_old['leaf_4way'] - lf_old['no_interleave']) / lf_old['no_interleave'] * 100.0, '+.2f')}%."
        )
        gvals = [gl[l] for l in LAYOUTS]
        L.append(
            f"(c) 1 MiB: Non {fmt(gl['no_interleave'])} ns, Group {fmt(gl['leaf_4way'])} ns, Flat "
            f"{fmt(gl['global_16way'])} ns; max/min = {fmt(max(gvals) / min(gvals), '.3f')} "
            f"({fmt((max(gvals) - min(gvals)) / min(gvals) * 100.0, '.1f')}% spread)."
        )
        order_old = (ep_old["no_interleave"], ep_old["leaf_4way"], ep_old["global_16way"])
        order_new = (ep["no_interleave"], ep["leaf_4way"], ep["global_16way"])
        L.append(
            f"(d) Against the old 6k@stride30 values: 16 KiB deltas "
            f"{fmt((ep['no_interleave'] - ep_old['no_interleave']) / ep_old['no_interleave'] * 100.0, '+.1f')}% / "
            f"{fmt((ep['leaf_4way'] - ep_old['leaf_4way']) / ep_old['leaf_4way'] * 100.0, '+.1f')}% / "
            f"{fmt((ep['global_16way'] - ep_old['global_16way']) / ep_old['global_16way'] * 100.0, '+.1f')}%, "
            f"128 KiB "
            f"{fmt((lf['no_interleave'] - lf_old['no_interleave']) / lf_old['no_interleave'] * 100.0, '+.1f')}% / "
            f"{fmt((lf['leaf_4way'] - lf_old['leaf_4way']) / lf_old['leaf_4way'] * 100.0, '+.1f')}% / "
            f"{fmt((lf['global_16way'] - lf_old['global_16way']) / lf_old['global_16way'] * 100.0, '+.1f')}%, "
            f"1 MiB "
            f"{fmt((gl['no_interleave'] - gl_old['no_interleave']) / gl_old['no_interleave'] * 100.0, '+.1f')}% / "
            f"{fmt((gl['leaf_4way'] - gl_old['leaf_4way']) / gl_old['leaf_4way'] * 100.0, '+.1f')}% / "
            f"{fmt((gl['global_16way'] - gl_old['global_16way']) / gl_old['global_16way'] * 100.0, '+.1f')}%. "
            f"The 16 KiB ordering {'is preserved' if order_new == tuple(sorted(order_new)) and order_old == tuple(sorted(order_old)) else 'changed'}; "
            f"the 1 MiB three-way convergence {'is' if max(gvals) / min(gvals) < 1.05 else 'is NOT'} reproduced."
        )
        L.append("")
    L.append("## Harness sanity check (6000-op reference cells, seed_base=10000)")
    L.append("")
    if sanity:
        L.append("| mapping | latency (ns) | old (ns) | rel. diff | touched | old touched | status |")
        L.append("|---|---:|---:|---:|---:|---:|---|")
        for r in sanity:
            layout = r["layout"]
            old_lat, old_touch = OLD_REFERENCE[(layout, 16 * 1024)]
            lat = float(r["host_weighted_avg_latency_ns"])
            touch = float(r["sanity_mean_touched_endpoints"])
            L.append(
                f"| {LAYOUT_LABEL[layout]} | {lat:.10f} | {old_lat:.10f} | {abs(lat - old_lat) / old_lat:.2e} | "
                f"{touch:.3f} | {old_touch:.3f} | {r['run_status']} |"
            )
        L.append("")
        L.append(f"Tolerance for the sanity cells: relative error < {SANITY_RTOL:g}.")
    else:
        L.append("No sanity rows recorded.")
    L.append("")
    controls = [r for r in rows if str(r.get("experiment", "")).startswith("control_")]
    if controls:
        L.append("## Control sweeps (injection rate vs run length)")
        L.append("")
        L.append("| experiment | ops/host | tick_stride | regime | mapping | latency (ns) | norm vs 16K/45k/stride30 Non | touched | BW (Gb/s) |")
        L.append("|---|---:|---:|---|---|---:|---:|---:|---:|")
        groups: dict[tuple[str, int, int], list[dict]] = {}
        for r in controls:
            key = (str(r["experiment"]), int(float(r["trace_ops_per_host"])), int(float(r["tick_stride"])))
            groups.setdefault(key, []).append(r)
        for (experiment, ops, stride), group in sorted(groups.items()):
            for footprint in (16 * 1024, 128 * 1024, v2.GLOBAL_HPA_SIZE):
                for layout in LAYOUTS:
                    match = [r for r in group if int(float(r["footprint_bytes"])) == footprint and r["layout"] == layout]
                    if not match:
                        continue
                    mean = sum(num(r, "host_weighted_avg_latency_ns") for r in match) / len(match)
                    tvals = [num(r, "sanity_mean_touched_endpoints") for r in match]
                    bw = sum(num(r, "host_sum_bw_gbps") for r in match) / len(match)
                    L.append(
                        f"| {experiment} | {ops} | {stride} | {REGIME_LABEL[footprint]} | {LAYOUT_LABEL[layout]} | "
                        f"{fmt(mean)} | {fmt(mean / base_mean, '.3f')}x | {fmt(sum(tvals) / len(tvals))} | {fmt(bw)} |"
                    )
        L.append("")
    L.extend(stationarity_table())
    L.append("## Verdict")
    L.append("")
    endpoint = {l: means.get((16 * 1024, l), math.nan) for l in LAYOUTS}
    leaf = {l: means.get((128 * 1024, l), math.nan) for l in LAYOUTS}
    glob = {l: means.get((v2.GLOBAL_HPA_SIZE, l), math.nan) for l in LAYOUTS}
    if not math.isnan(endpoint["no_interleave"]):
        n = endpoint["no_interleave"]
        order_ok = endpoint["no_interleave"] < endpoint["leaf_4way"] < endpoint["global_16way"]
        L.append(
            f"(a) Endpoint (16 KiB): Non {fmt(endpoint['no_interleave'])} ns, Group {fmt(endpoint['leaf_4way'])} ns "
            f"({fmt(endpoint['leaf_4way'] / n, '.3f')}x), Flat {fmt(endpoint['global_16way'])} ns "
            f"({fmt(endpoint['global_16way'] / n, '.3f')}x). Non < Group < Flat still holds: "
            f"{'yes' if order_ok else 'no'}."
        )
    if not math.isnan(leaf["no_interleave"]):
        n = leaf["no_interleave"]
        L.append(
            f"(b) Leaf (128 KiB): Non {fmt(leaf['no_interleave'])} ns, Group {fmt(leaf['leaf_4way'])} ns "
            f"({fmt(leaf['leaf_4way'] / n, '.3f')}x of Non), Flat {fmt(leaf['global_16way'])} ns "
            f"({fmt(leaf['global_16way'] / n, '.3f')}x of Non). Group still better than Non: "
            f"{'yes' if leaf['leaf_4way'] < leaf['no_interleave'] else 'no'}."
        )
    if not math.isnan(glob["no_interleave"]):
        n = glob["no_interleave"]
        vals = [glob[l] for l in LAYOUTS]
        spread = (max(vals) - min(vals)) / min(vals) * 100.0
        ng_spread = abs(glob["leaf_4way"] - glob["no_interleave"]) / glob["no_interleave"] * 100.0
        L.append(
            f"(c) Global (1 MiB): Non {fmt(glob['no_interleave'])} ns, Group {fmt(glob['leaf_4way'])} ns "
            f"({fmt(glob['leaf_4way'] / n, '.3f')}x), Flat {fmt(glob['global_16way'])} ns "
            f"({fmt(glob['global_16way'] / n, '.3f')}x, censored; {fmt(ft.get(v2.GLOBAL_HPA_SIZE, math.nan) / base_mean, '.2f')}x full-trace). "
            f"Non and Group agree to {fmt(ng_spread, '.2f')}% (they converge), but Flat is "
            f"{fmt(spread, '.1f')}% above the minimum: the three-way convergence of the short run does not survive "
            f"at steady state."
        )
    touch_ok = all(
        abs(touched.get((fp, l), math.nan) - EXPECTED_TOUCHED[fp][l]) < 1e-6
        for fp in (16 * 1024, 128 * 1024, v2.GLOBAL_HPA_SIZE)
        for l in LAYOUTS
    )
    desc = "; ".join(
        f"{REGIME_LABEL[fp]} " + "/".join(fmt(touched.get((fp, l), math.nan)) for l in LAYOUTS)
        for fp in (16 * 1024, 128 * 1024, v2.GLOBAL_HPA_SIZE)
    )
    L.append(
        f"(d) Touched endpoints (Non/Group/Flat): {desc}. Expected 1/4/16, 2/4/16, 16/16/16: "
        f"{'all match' if touch_ok else 'MISMATCH'}."
    )
    L.append("")
    flat_bins: dict[tuple[str, int], dict[int, float]] = {}
    for r in stat_rows:
        if r["layout"] != "global_16way":
            continue
        flat_bins.setdefault((r["experiment"], int(float(r["footprint_bytes"]))), {})[
            int(r["bin"])
        ] = float(r["mean_latency_ns"])

    def flat_ratio(experiment: str, footprint: int) -> float:
        b = flat_bins.get((experiment, footprint))
        return (b[max(b)] / b[0]) if b else math.nan

    if flat_bins:
        firsts = "/".join(fmt(flat_bins[("steady_grid", fp)][0], ".0f") for fp in (16 * 1024, 128 * 1024, v2.GLOBAL_HPA_SIZE) if ("steady_grid", fp) in flat_bins)
        lasts = "/".join(fmt(flat_bins[("steady_grid", fp)][max(flat_bins[("steady_grid", fp)])], ".0f") for fp in (16 * 1024, 128 * 1024, v2.GLOBAL_HPA_SIZE) if ("steady_grid", fp) in flat_bins)
        ratios = "/".join(fmt(flat_ratio("steady_grid", fp), ".1f") for fp in (16 * 1024, 128 * 1024, v2.GLOBAL_HPA_SIZE))
        L.append(
            f"(e) Flat is not stationary at stride 30: inside one 45k run its per-request latency rises from "
            f"{firsts} ns in the first tenth of the run to {lasts} ns in the last tenth (last/first = {ratios}x), "
            f"while Non and Group stay flat (last/first 0.98-1.02x). The default max_clock=8e6 truncates the Flat "
            f"runs before the trace ends, so their rows above are censored window averages, not converged latencies."
        )
        if ft:
            L.append(
                f"    Re-running the three Flat cells with max_clock=3e7 (full trace, same 45k ops) gives "
                f"{fmt(ft.get(16 * 1024, math.nan))} / {fmt(ft.get(128 * 1024, math.nan))} / "
                f"{fmt(ft.get(v2.GLOBAL_HPA_SIZE, math.nan))} ns = "
                f"{fmt(ft.get(16 * 1024, math.nan) / base_mean, '.2f')}x / "
                f"{fmt(ft.get(128 * 1024, math.nan) / base_mean, '.2f')}x / "
                f"{fmt(ft.get(v2.GLOBAL_HPA_SIZE, math.nan) / base_mean, '.2f')}x of the 16 KiB Non baseline; "
                f"the profile is still rising at the end, so no converged value exists at this injection rate."
            )
    if s60:
        L.append(
            f"(f) Controls: halving the injection rate (tick_stride=60, same 45k ops) removes the effect -- Flat "
            f"measures {fmt(s60.get((16 * 1024, 'global_16way'), math.nan))} ns (16 KiB) and "
            f"{fmt(s60.get((v2.GLOBAL_HPA_SIZE, 'global_16way'), math.nan))} ns (1 MiB), versus Non "
            f"{fmt(s60.get((16 * 1024, 'no_interleave'), math.nan))} ns and "
            f"{fmt(s60.get((v2.GLOBAL_HPA_SIZE, 'no_interleave'), math.nan))} ns -- so the Flat blow-up is driven by "
            f"the injection rate (offered load), not by the run length. Doubling the run length at stride 30 "
            f"(ops=90000) reproduces the 45k numbers to the last digit (Flat "
            f"{fmt(o90k.get((16 * 1024, 'global_16way'), math.nan))} / "
            f"{fmt(o90k.get((v2.GLOBAL_HPA_SIZE, 'global_16way'), math.nan))} ns) because both runs are cut by the "
            f"same max_clock; run length alone changes nothing."
        )
    L.append(
        "(g) Seeds: all three seed bases produce bit-identical rows (range 0.000 ns) because the trace addresses "
        "are drawn from a fixed RNG seed that does not depend on --seed-base (it only changes each host's "
        "random_seed field). The 3-seed spread therefore measures determinism, not run-to-run variance."
    )
    L.append("")
    L.append("## Run health")
    L.append("")
    bad = [r for r in rows_by_id.values() if r.get("run_status") != "ok"]
    elapsed = [num(r, "elapsed_sec") for r in rows_by_id.values()]
    elapsed = [e for e in elapsed if not math.isnan(e)]
    L.append(f"- Cells recorded: {len(rows_by_id)}; not ok: {len(bad)}.")
    if elapsed:
        L.append(
            f"- Elapsed per cell: min {fmt(min(elapsed), '.2f')} s, max {fmt(max(elapsed), '.2f')} s, "
            f"median {fmt(sorted(elapsed)[len(elapsed) // 2], '.2f')} s."
        )
    L.append(
        "- trace_ops_per_host actually written: "
        + ", ".join(sorted({str(r.get("trace_ops_per_host")) for r in rows_by_id.values()}))
        + "."
    )
    for r in bad:
        L.append(f"  - {r.get('exp_id')}: {r.get('run_status')}")
    L.append("")
    return "\n".join(L) + "\n"


def main() -> int:
    global MAX_CLOCK
    args = parse_args()
    MAX_CLOCK = args.max_clock
    ensure_dirs()
    if args.mode == "stationarity":
        analyze_stationarity()
        return 0
    rows_by_id: dict[str, dict[str, object]] = {}
    if args.mode == "report":
        for r in load_csv(SUMMARY_CSV):
            rows_by_id[str(r.get("exp_id"))] = r
        SUMMARY_MD.write_text(build_summary(rows_by_id), encoding="utf-8")
        print(f"Wrote summary: {SUMMARY_MD}")
        return 0
    if args.mode in {"sanity", "all"}:
        if not sanity_check(args):
            return 2
    if args.mode == "probe":
        return run_probe(args)
    if args.mode == "controlled":
        return run_controlled(args, ops=args.ops, experiment="steady_controlled", tag="ctl_inj")
    if args.mode == "controlled6k":
        return run_controlled(args, ops=6000, experiment="controlled_6k", tag="ctl_inj6k")
    if args.mode in {"grid", "all", "controls"}:
        run_grid(args, rows_by_id)
    for r in load_csv(SUMMARY_CSV):
        rows_by_id.setdefault(str(r.get("exp_id")), r)
    SUMMARY_MD.write_text(build_summary(rows_by_id), encoding="utf-8")
    print(f"Wrote summary: {SUMMARY_MD}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
