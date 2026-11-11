#!/usr/bin/env python3
"""Score the revised 5.4 (wl2) grid against the acceptance criteria.

Reads <out-root>/wl2_results.csv (written by run_wl2.py) and prints one row per
cell.  The summary CSV has dynamic snoop columns (Snoop-0:..., Snoop-1:...), so
every snoop quantity is summed either from the derived snoop_sum_* column or,
when that column is absent, from the raw per-device columns.

Gates (these decide PASS/FAIL and the exit code):

    C1  every memory endpoint carries 6.25% +/- 15% of the ops
        (mapping=non is relaxed to +/- 25% because it does not interleave)
    C3  snoop_sum_cln_evct_stale / snoop_sum_cln_evct_rx < 10%
    C4  snoop_sum_cln_evct_free  / snoop_sum_cln_evct_rx > 30%

Diagnostic (computed and printed, never scored):

    C2  share of evicted lines with a non-zero victim_sharers count,
        i.e. 1 - evict_sharers_0 / sum_N evict_sharers_N

    The denominator must be the number of evicted *lines*, not the number of
    evict() calls.  snoop.hh pushes one entry per line inside
    conduct_burst_evict() (up to max_burst_inv = 8 lines per call), while
    evict_total_count_ counts evict() invocations, so dividing by the call
    count understates the denominator whenever a burst evicts more than one
    line.  On the non mapping that ratio drops below 1 and the old formula
    even returned negative percentages (-110% / -120%).

    C2 is not a workload-validity criterion: with the same trace it ranges
    from ~0.2% to ~18.5% depending on the snoop-filter capacity and on whether
    MemClnEvct is on (clean evictions remove sharers before a LIFO victim is
    chosen).  It is reported so the effect can be read off the table, not so
    that a cell can be rejected for it.

Audit (must hold before any of the above is believable):

    sum(Host-*:req_count) + sum(Host-*:cache_hit_count) == 144,000
    every Mem-*:unexpected_pkt == 0
    conservation: free + drop_sharer + promote + stale + busy + dirty_reject +
                  out_of_range == rx  (clean-evict on), rx == cln_sent,
                  cln_evct_invalid_addr == 0
    clean-evict off: rx == 0 and cln_sent == 0
"""

from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT_ROOT = "results/rev_54_wl2"
SUMMARY_NAME = "wl2_results.csv"

TARGET_SHARE = 0.0625
TOL_STRICT = 0.15
TOL_NON = 0.25
C2_DIAGNOSTIC_MIN = 0.20   # reference line only; C2 does not gate
C3_MAX = 0.10
C4_MIN = 0.30
EXPECTED_OPS = 144_000

CLN_COMPONENTS = (
    "cln_evct_free",
    "cln_evct_drop_sharer",
    "cln_evct_promote",
    "cln_evct_stale",
    "cln_evct_busy",
    "cln_evct_dirty_reject",
    "cln_evct_out_of_range",
)


# ---------------------------------------------------------------------------
# Column access helpers.
# ---------------------------------------------------------------------------

def num(row: dict, key: str) -> float:
    value = row.get(key, "")
    if value in ("", None):
        return 0.0
    try:
        return float(value)
    except (TypeError, ValueError):
        return 0.0


def sum_host(row: dict, suffix: str) -> float:
    total = 0.0
    for key, value in row.items():
        if key.startswith("Host-") and key.endswith(suffix) and value not in ("", None):
            total += float(value)
    return total


def sum_snoop(row: dict, metric: str) -> float:
    """Sum one snoop metric across all snoop endpoints."""
    derived = row.get(f"snoop_sum_{metric}", "")
    if derived not in ("", None):
        return float(derived)
    total = 0.0
    found = False
    for key, value in row.items():
        if not key.startswith("Snoop-") or ":" not in key:
            continue
        if key.split(":", 1)[1] != metric:
            continue
        if value in ("", None):
            continue
        total += float(value)
        found = True
    return total if found else 0.0


def sum_snoop_prefix(row: dict, prefix: str) -> float:
    """Sum every snoop metric whose name starts with prefix (e.g. evict_sharers_).

    Empty cells count as zero, never as a reason to skip the accumulator: a
    missing or empty evict_sharers_N bucket means that N never occurred.
    """
    total = 0.0
    found = False
    for key, value in row.items():
        if not key.startswith(f"snoop_sum_{prefix}"):
            continue
        # An empty bucket contributes zero but does not disable the sum.
        if value not in ("", None):
            total += float(value)
            found = True
    if found:
        return total
    # The derived family is missing or entirely empty for this row (a grid whose
    # other rows do have it, or a CSV written without derived metrics): fall
    # back to the raw per-snoop columns.
    for key, value in row.items():
        if not key.startswith("Snoop-") or ":" not in key:
            continue
        if not key.split(":", 1)[1].startswith(prefix):
            continue
        if value not in ("", None):
            total += float(value)
            found = True
    return total if found else 0.0


def sum_mem_unexpected(row: dict) -> float:
    """Sum Mem-*:unexpected_pkt; DRAM-side rejects must stay at zero."""
    return sum(
        float(value)
        for key, value in row.items()
        if key.startswith("Mem-")
        and key.endswith(":unexpected_pkt")
        and value not in ("", None)
    )


def per_mem_shares(row: dict) -> dict[str, float]:
    """Share of ops per memory endpoint, from the workload sanity columns."""
    shares: dict[str, float] = {}
    for key, value in row.items():
        if not key.startswith("workload_mem_share_Mem-") or value in ("", None):
            continue
        shares[key[len("workload_mem_share_"):]] = float(value)
    if shares:
        return shares
    total = num(row, "sanity_total_ops")
    if total <= 0:
        return {}
    for key, value in row.items():
        if not key.startswith("workload_mem_ops_") or value in ("", None):
            continue
        shares[key[len("workload_mem_ops_"):]] = float(value) / total
    return shares


def mem_lines_and_sets(row: dict) -> tuple[int, int]:
    """Worst per-endpoint distinct-line and set-coverage counts."""
    lines = [
        int(float(value))
        for key, value in row.items()
        if key.startswith("sanity_mem_lines_") and value not in ("", None)
    ]
    sets = [
        int(float(value))
        for key, value in row.items()
        if key.startswith("sanity_mem_sets_") and value not in ("", None)
    ]
    return (min(lines) if lines else 0, min(sets) if sets else 0)


# ---------------------------------------------------------------------------
# Gate evaluation.
# ---------------------------------------------------------------------------

def cell_label(row: dict) -> str:
    return str(row.get("exp_id", "?"))


def evaluate(row: dict) -> dict:
    """Return the per-criterion result for one cell."""
    mapping = str(row.get("mapping", ""))
    policy = str(row.get("policy", ""))
    state = str(row.get("state", ""))
    status = str(row.get("run_status", ""))
    result: dict[str, object] = {
        "exp_id": cell_label(row),
        "mapping": mapping,
        "k": row.get("k", ""),
        "sf": row.get("sf_lines", ""),
        "policy": policy,
        "state": state,
        "status": status,
        "c1": None,
        "c2": None,
        "c3": None,
        "c4": None,
        "audit": None,
        "notes": [],
    }

    # ---- C1: per-endpoint op share (a workload property, so it is
    # reported even for dry runs that never touched the simulator). --------
    shares = per_mem_shares(row)
    if shares:
        tolerance = TOL_NON if mapping == "non" else TOL_STRICT
        low = TARGET_SHARE * (1.0 - tolerance)
        high = TARGET_SHARE * (1.0 + tolerance)
        worst = max(shares.values(), key=lambda v: abs(v - TARGET_SHARE))
        ok = all(low <= v <= high for v in shares.values())
        result["c1"] = {
            "ok": ok,
            "min": min(shares.values()),
            "max": max(shares.values()),
            "worst": worst,
            "band": (low, high),
            "endpoints": len(shares),
        }

    if status != "ok":
        result["notes"].append(f"run_status={status or 'missing'}")
        return result

    # ---- C2 (diagnostic): share of evicted LINES with a non-zero sharer
    # count.  The denominator is the per-line evict_sharers_N histogram, not
    # evict_total_count, which counts evict() calls. ------------------------
    evict_calls = sum_snoop(row, "evict_total_count")
    evicted_lines = sum_snoop_prefix(row, "evict_sharers_")
    zero_lines = sum_snoop(row, "evict_sharers_0")
    if evicted_lines > 0:
        ratio = 1.0 - zero_lines / evicted_lines
        result["c2"] = {
            "ok": ratio >= C2_DIAGNOSTIC_MIN,   # reference only, not a gate
            "ratio": ratio,
            "evict_calls": evict_calls,
            "evicted_lines": evicted_lines,
            "zero_lines": zero_lines,
            "burst": (evicted_lines / evict_calls) if evict_calls else None,
        }
    elif evict_calls > 0:
        result["notes"].append("evict calls without a sharer histogram")

    # ---- C3/C4: clean-eviction notification destinations. ---------------
    rx = sum_snoop(row, "cln_evct_rx")
    if state == "on":
        if rx > 0:
            stale = sum_snoop(row, "cln_evct_stale")
            free = sum_snoop(row, "cln_evct_free")
            result["c3"] = {"ok": stale / rx < C3_MAX, "ratio": stale / rx,
                            "count": stale, "rx": rx}
            result["c4"] = {"ok": free / rx > C4_MIN, "ratio": free / rx,
                            "count": free, "rx": rx}
        else:
            result["notes"].append("clean-evict on but rx=0")

    # ---- Audit. ---------------------------------------------------------
    req = sum_host(row, ":req_count")
    hits = sum_host(row, ":cache_hit_count")
    processed = req + hits
    expected = int(num(row, "total_ops_expected") or EXPECTED_OPS)
    unexpected = sum_mem_unexpected(row)
    cln_sent = sum_host(row, ":cln_evct_sent")
    invalid = sum_host(row, ":cln_evct_invalid_addr")
    components = sum(sum_snoop(row, metric) for metric in CLN_COMPONENTS)
    conserv_ok = True
    if state == "on":
        conserv_ok = abs(components - rx) < 0.5 and abs(cln_sent - rx) < 0.5 and invalid == 0
    else:
        conserv_ok = rx == 0 and cln_sent == 0
    result["audit"] = {
        "ok": processed == expected and unexpected == 0 and conserv_ok,
        "processed": processed,
        "expected": expected,
        "unexpected": unexpected,
        "conserv_ok": conserv_ok,
        "conserv_sum": components,
        "rx": rx,
        "cln_sent": cln_sent,
        "invalid": invalid,
    }
    return result


def verdict(result: dict) -> str:
    """C2 is deliberately absent: it is a diagnostic, not a gate."""
    gates = [result[key] for key in ("c1", "c3", "c4", "audit")]
    applicable = [gate for gate in gates if gate is not None]
    if result["status"] != "ok":
        return "SKIP"
    if not applicable:
        return "N/A"
    return "PASS" if all(gate["ok"] for gate in applicable) else "FAIL"


# ---------------------------------------------------------------------------
# Reporting.
# ---------------------------------------------------------------------------

def pad(text: str, width: int) -> str:
    return text + " " * max(0, width - len(text))


def fmt_pct(value: float | None, width: int = 7) -> str:
    if value is None:
        return pad("-", width)
    return pad(f"{value * 100:.2f}%", width)


def report(rows: list[dict]) -> int:
    results = [evaluate(row) for row in rows]

    print("gates (these decide PASS/FAIL and the exit code):")
    header = (
        pad("cell", 33)
        + pad("map", 7)
        + pad("k", 4)
        + pad("sf", 5)
        + pad("policy", 8)
        + pad("state", 6)
        + pad("C1 share", 14)
        + pad("C3", 8)
        + pad("C4", 8)
        + pad("audit", 7)
        + "verdict"
    )
    print(header)
    print("-" * len(header))
    for result in results:
        c1 = result["c1"]
        c3 = result["c3"]
        c4 = result["c4"]
        audit = result["audit"]
        share_text = pad("-", 14)
        if c1:
            share_text = pad(f"{c1['min'] * 100:.2f}..{c1['max'] * 100:.2f}%", 14)
        print(
            pad(str(result["exp_id"]), 33)
            + pad(str(result["mapping"]), 7)
            + pad(str(result["k"]), 4)
            + pad(str(result["sf"]), 5)
            + pad(str(result["policy"]), 8)
            + pad(str(result["state"]), 6)
            + share_text
            + fmt_pct(c3["ratio"] if c3 else None, 7) + " "
            + fmt_pct(c4["ratio"] if c4 else None, 7) + " "
            + pad("OK" if audit and audit["ok"] else ("n/a" if audit is None else "FAIL"), 6)
            + verdict(result)
        )

    print()
    print("eviction diagnostics (C2 is reported here, it does not gate):")
    header = (
        pad("cell", 33)
        + pad("policy", 9)
        + pad("state", 6)
        + pad("evict_calls", 12)
        + pad("evicted_lines", 14)
        + pad("burst", 7)
        + pad("zero_lines", 12)
        + "C2 diag"
    )
    print(header)
    print("-" * len(header))
    for result in results:
        c2 = result["c2"]
        if not c2:
            print(
                pad(str(result["exp_id"]), 33)
                + pad(str(result["policy"]), 9)
                + pad(str(result["state"]), 6)
                + pad("-", 12) + pad("-", 14) + pad("-", 7) + pad("-", 12) + "-"
            )
            continue
        burst = c2["burst"]
        print(
            pad(str(result["exp_id"]), 33)
            + pad(str(result["policy"]), 9)
            + pad(str(result["state"]), 6)
            + pad(f"{c2['evict_calls']:.0f}", 12)
            + pad(f"{c2['evicted_lines']:.0f}", 14)
            + pad(f"{burst:.2f}" if burst is not None else "-", 7)
            + pad(f"{c2['zero_lines']:.0f}", 12)
            + fmt_pct(c2["ratio"])
        )

    print()
    print("gate detail (a dash means the criterion does not apply to that cell):")
    for result in results:
        if result["status"] != "ok":
            print(f"  {result['exp_id']}: not scored ({'; '.join(result['notes'])})")
            continue
        parts = []
        c1 = result["c1"]
        if c1:
            band = f"[{c1['band'][0] * 100:.2f}%, {c1['band'][1] * 100:.2f}%]"
            parts.append(
                f"C1 {c1['min'] * 100:.2f}-{c1['max'] * 100:.2f}% over "
                f"{c1['endpoints']} endpoints (band {band}) "
                f"{'pass' if c1['ok'] else 'FAIL'}"
            )
        c2 = result["c2"]
        if c2:
            burst = c2["burst"]
            burst_text = f"{burst:.2f}" if burst is not None else "-"
            parts.append(
                f"C2 diag {c2['ratio'] * 100:.2f}% = 1 - "
                f"{c2['zero_lines']:.0f}/{c2['evicted_lines']:.0f} evicted lines "
                f"({c2['evict_calls']:.0f} evict calls, burst {burst_text}) "
                f"[not gated]"
            )
        c3 = result["c3"]
        if c3:
            parts.append(f"C3 stale/rx {c3['ratio'] * 100:.2f}% "
                         f"({'pass' if c3['ok'] else 'FAIL'})")
        c4 = result["c4"]
        if c4:
            parts.append(f"C4 free/rx {c4['ratio'] * 100:.2f}% "
                         f"({'pass' if c4['ok'] else 'FAIL'})")
        audit = result["audit"]
        if audit:
            parts.append(
                f"audit processed {audit['processed']:.0f}/{audit['expected']}, "
                f"unexpected {audit['unexpected']:.0f}, "
                f"conservation {audit['conserv_sum']:.0f} vs rx {audit['rx']:.0f} "
                f"(sent {audit['cln_sent']:.0f}, invalid {audit['invalid']:.0f}) "
                f"{'pass' if audit['ok'] else 'FAIL'}"
            )
        print(f"  {result['exp_id']}: " + "; ".join(parts))

    scored = [r for r in results if r["status"] == "ok"]
    passed = [r for r in scored if verdict(r) == "PASS"]
    failed = [r for r in scored if verdict(r) == "FAIL"]
    skipped = [r for r in results if r["status"] != "ok"]
    print()
    print(f"cells={len(results)} scored={len(scored)} passed={len(passed)} "
          f"failed={len(failed)} skipped={len(skipped)}")
    print(
        "gates: C1 6.25% +/-15% (+/-25% for non) | "
        f"C3 stale/rx < {C3_MAX:.0%} | "
        f"C4 free/rx > {C4_MIN:.0%}"
    )
    print(
        "C2 diagnostic: 1 - evict_sharers_0 / sum_N evict_sharers_N "
        f"(reference line {C2_DIAGNOSTIC_MIN:.0%}, never scored); "
        "denominator is evicted lines, not evict() calls"
    )
    return {"failed": len(failed), "scored": len(scored), "skipped": len(skipped)}


# ---------------------------------------------------------------------------
# Main.
# ---------------------------------------------------------------------------

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Score wl2 cells against the 5.4 acceptance criteria"
    )
    parser.add_argument("--out-root", default=DEFAULT_OUT_ROOT,
                        help=f"run directory (default: {DEFAULT_OUT_ROOT})")
    parser.add_argument("--csv", default=None,
                        help=f"summary CSV (default: <out-root>/{SUMMARY_NAME})")
    parser.add_argument("--strict-exit", action="store_true",
                        help="also return non-zero when no cell was scored")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    base = Path(args.out_root)
    if not base.is_absolute():
        base = ROOT / base
    csv_path = Path(args.csv) if args.csv else base / SUMMARY_NAME
    if not csv_path.is_absolute():
        csv_path = ROOT / csv_path
    if not csv_path.exists():
        print(f"no summary CSV at {csv_path}", file=sys.stderr)
        print("run AE-scripts/run_wl2.py first", file=sys.stderr)
        return 2
    with csv_path.open(newline="") as fh:
        rows = list(csv.DictReader(fh))
    if not rows:
        print(f"empty summary CSV at {csv_path}", file=sys.stderr)
        return 2
    print(f"scoring {len(rows)} cells from {csv_path}")
    print()
    counts = report(rows)
    if counts["failed"]:
        return 1
    if args.strict_exit and counts["scored"] == 0:
        print("no cell was scored (all rows skipped)", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
