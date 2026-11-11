#!/usr/bin/env python3
"""Generate the Section 5.3 cells used by Figures 19 and 20.

Cells (written under --out-root, default results/repro_fig16_23):

    s53/policy_V_<POL>/                 Figure 19 left,  ClnEvct off
    s53_on/policy_V_<POL>/              Figure 19 right, ClnEvct on (same trace)
    s53_small/<POL>_<off|on>/           Figure 19 right, 512-line cache / 1,024-line SF
    s53_invblk/n<N>_<off|on>_s<S>/      Figure 20, InvBlk length N, 3 seeds

The two TOML templates live in configs/repro/.  Only log paths, the eviction
policy, the InvBlk length, the clean-eviction switch, the cache/SF size and the
random seeds are rewritten; every other key is the archived working point.

    python3 AE-scripts/run_s53_figures.py --jobs 4
    python3 AE-scripts/run_s53_figures.py --only policy --quick
"""
from __future__ import annotations

import argparse
import concurrent.futures as cf
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "build" / "Xerxes"
TPL = ROOT / "configs" / "repro"
POL5 = ("FIFO", "LRU", "LFI", "LIFO", "MRU")
SEEDS = (1, 2, 3)
NS = (1, 2, 3, 4, 8)


def one(pattern: str, repl: str, text: str, count: int = 1) -> str:
    new, n = re.subn(pattern, repl, text, count=count)
    assert n == count, (pattern, n, count)
    return new


def render(template: str, *, cell: Path, eviction: str | None = None,
           small: bool = False, notify: bool = False, burst: int | None = None,
           seed_base: int | None = None) -> str:
    text = (TPL / template).read_text()
    rel = cell.relative_to(ROOT).as_posix()
    text = one(r'log_name = ".*"', f'log_name = "{rel}/main.csv"', text)
    text = one(r'output_dir = ".*"', f'output_dir = "{rel}/dram"', text)
    text = re.sub(r'clean_evict_notify = (?:true|false)',
                  f'clean_evict_notify = {"true" if notify else "false"}', text)
    if eviction is not None:
        text = one(r'eviction = ".*"', f'eviction = "{eviction}"', text)
    if small:
        text = one(r'line_num = \d+', "line_num = 1024", text)
        text = one(r'cache_capacity = \d+', "cache_capacity = 512", text)
    if burst is not None:
        text = one(r'max_burst_inv = \d+', f"max_burst_inv = {burst}", text)
    if seed_base is not None:
        counter = iter(seed_base + i for i in range(1, 100))

        def seed(_m):
            return f"random_seed = {next(counter)}"

        text = re.sub(r'random_seed = \d+', seed, text)
    return text


def cells(quick: bool):
    out = []
    for pol in POL5:
        out.append(("policy", f"s53/policy_V_{pol}", "s53_policy.toml",
                    dict(eviction=pol), 0))
        out.append(("policy", f"s53_on/policy_V_{pol}", "s53_policy.toml",
                    dict(eviction=pol, notify=True), 0))
        out.append(("small", f"s53_small/{pol}_off", "s53_policy.toml",
                    dict(eviction=pol, small=True), 0))
        out.append(("small", f"s53_small/{pol}_on", "s53_policy.toml",
                    dict(eviction=pol, small=True, notify=True), 0))
    for n in NS:
        for state in ("off", "on"):
            for seed in (SEEDS if not quick else (SEEDS[0],)):
                out.append(("invblk",
                            f"s53_invblk/n{n}_{state}_s{seed}",
                            "s53_invblk.toml",
                            dict(burst=n, notify=(state == "on"),
                                 seed_base=seed * 10000),
                            0))
    return out


def run_cell(item, root: Path, force: bool, dry_run: bool):
    kind, rel, template, kw, _ = item
    cell = root / rel
    cfg = cell / "config.toml"
    out = cell / "main.csv"
    if out.is_file() and not force:
        return rel, "skip", 0.0
    cell.mkdir(parents=True, exist_ok=True)
    cfg.write_text(render(template, cell=cell, **kw))
    if dry_run:
        return rel, "dry", 0.0
    t0 = time.time()
    proc = subprocess.run([str(BIN), str(cfg)], cwd=ROOT,
                          capture_output=True, text=True, timeout=1800)
    (cell / "stdout.txt").write_text(proc.stdout + "\n---STDERR---\n" + proc.stderr)
    return rel, ("ok" if proc.returncode == 0 and out.is_file() else "FAIL"), time.time() - t0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-root", default="results/repro_fig16_23",
                        help="relative to the repo root")
    parser.add_argument("--only", default="policy,small,invblk",
                        help="comma-separated: policy,small,invblk")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--quick", action="store_true",
                        help="one seed for the InvBlk sweep")
    args = parser.parse_args()
    root = (ROOT / args.out_root).resolve()
    wanted = {x.strip() for x in args.only.split(",") if x.strip()}
    todo = [c for c in cells(args.quick) if c[0] in wanted]
    if not BIN.is_file() and not args.dry_run:
        print(f"missing simulator: {BIN}; build it first", file=sys.stderr)
        return 1
    with cf.ThreadPoolExecutor(args.jobs) as ex:
        for rel, status, dt in ex.map(lambda c: run_cell(c, root, args.force, args.dry_run), todo):
            print(f"{status:4s} {dt:6.1f}s  {rel}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
