#!/usr/bin/env python3
"""Generate the Section 5.4 cells used by Figures 21-23.

All cells run the wl2 workload on the paper's E5 bus topology
(16-requester spine-leaf, delay_per_T = 4) through run_wl2_bus.py.

Axes (written under --out-root, default results/repro_fig16_23/s54):

    deg_n<N>_s<S>/          sharing participant degree, n = 1/4/8/16
    cap_sf<SF>_s<S>/        SF capacity sweep, 11 points (0.22x - 3.56x)
    a3_sf512_r<R>_s<S>/     shared-data ratio, R = 0/10/.../90 %
    ctl_nullsf_n<N>_s10000/ bare (no SF) baseline, SF 65,537 lines x 1 way

    python3 AE-scripts/run_s54_figures.py --jobs 3
    python3 AE-scripts/run_s54_figures.py --only degree --quick
"""
from __future__ import annotations

import argparse
import concurrent.futures as cf
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / "AE-scripts" / "run_wl2_bus.py"
DEFAULT_ROOT = "results/repro_fig16_23/s54"
CAP_SF = (128, 256, 384, 448, 512, 544, 576, 640, 768, 1024, 2048)
RATIOS = (0, 10, 25, 40, 50, 65, 80, 90)
DEGREES = (1, 4, 8, 16)
SEEDS = (10000, 20000, 30000)

COMMON = [
    "--mappings", "global", "--k", "16", "--assoc", "4",
    "--cache-lines", "8192", "--private-lines", "64", "--alpha", "1.2",
    "--ops-per-host", "45000", "--max-clock", "40000000", "--timeout", "2400",
    "--policies", "LIFO,FS+LIFO", "--clean-evict", "off", "--index-hash",
]


def cells(quick: bool):
    seeds = SEEDS if not quick else (SEEDS[0],)
    out = []
    for n in (DEGREES if not quick else (DEGREES[0], DEGREES[-1])):
        for seed in seeds:
            out.append((f"deg_n{n}_s{seed}", int(seed),
                        ["--sharers", str(n), "--sf", "256", "--shared-ratio", "0.5"]))
    for sf in (CAP_SF if not quick else (256, 512, 2048)):
        for seed in seeds:
            out.append((f"cap_sf{sf}_s{seed}", int(seed),
                        ["--sharers", "16", "--sf", str(sf), "--shared-ratio", "0.5"]))
    for rp in (RATIOS if not quick else (0, 50, 90)):
        for seed in seeds:
            out.append((f"a3_sf512_r{rp}_s{seed}", int(seed),
                        ["--sharers", "16", "--sf", "512",
                         "--shared-ratio", str(rp / 100.0)]))
    for n in ((1, 16) if not quick else (1,)):
        out.append((f"ctl_nullsf_n{n}_s10000", 10000,
                    ["--sharers", str(n), "--sf", "65537", "--assoc", "1",
                     "--shared-ratio", "0.5", "--policies", "LIFO"]))
    return out


def run_cell(item, root: Path, force: bool):
    tag, seed, extra = item
    cell = root / tag
    if (cell / "wl2_results.csv").is_file() and not force:
        return tag, "skip", 0.0
    args = [sys.executable, str(RUNNER), "--out-root", str(root), "--tag", tag,
            "--seed-base", str(seed), *COMMON, *extra]
    env = dict(os.environ, XLINK="E5", XLINK_DELAY="4")
    t0 = time.time()
    proc = subprocess.run(args, cwd=ROOT, env=env, capture_output=True, text=True,
                          timeout=7200)
    cell.mkdir(parents=True, exist_ok=True)
    (cell / "stdout.txt").write_text(proc.stdout + "\n---STDERR---\n" + proc.stderr)
    ok = proc.returncode == 0 and (cell / "wl2_results.csv").is_file()
    return tag, ("ok" if ok else "FAIL"), time.time() - t0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-root", default=DEFAULT_ROOT,
                        help="relative to the repo root")
    parser.add_argument("--only", default="degree,capacity,ratio,control",
                        help="comma-separated: degree,capacity,ratio,control")
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--quick", action="store_true")
    args = parser.parse_args()
    root = (ROOT / args.out_root).resolve()
    wanted = {x.strip() for x in args.only.split(",") if x.strip()}
    todo = []
    for tag, seed, extra in cells(args.quick):
        if tag.startswith("deg_") and "degree" not in wanted:
            continue
        if tag.startswith("cap_") and "capacity" not in wanted:
            continue
        if tag.startswith("a3_") and "ratio" not in wanted:
            continue
        if tag.startswith("ctl_") and "control" not in wanted:
            continue
        todo.append((tag, seed, extra))
    with cf.ThreadPoolExecutor(args.jobs) as ex:
        for tag, status, dt in ex.map(lambda c: run_cell(c, root, args.force), todo):
            print(f"{status:4s} {dt:6.1f}s  {tag}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
