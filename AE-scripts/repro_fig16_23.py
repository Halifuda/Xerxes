#!/usr/bin/env python3
"""One entry point for Figures 16-23: run the cells, then draw the figures.

    python3 AE-scripts/repro_fig16_23.py --jobs 4
    python3 AE-scripts/repro_fig16_23.py --figs 19,20 --quick     # smoke test
    python3 AE-scripts/repro_fig16_23.py --skip-data --fig-root ../figs

Data land in results/repro_fig16_23/ and EPS files in figures-repro/ by
default.  Each EPS is printed with the path it must take inside the LaTeX
tree, so the paper can be rebuilt by copying eight files:

    fig0609_placement_perfect.eps        -> fig/final-0609-figures/
    fig0609_footprint_regimes.eps        -> fig/final-0609-figures/
    fig0609_group_size_sensitivity.eps   -> fig/final-0609-figures/
    fig_repro_s53_policy.eps             -> fig/test/
    fig_repro_s53_invblk.eps             -> fig/test/
    fig_repro_s54_degree.eps             -> fig/test/
    fig_repro_s54_capacity.eps           -> fig/test/
    fig_repro_s54_ratio.eps              -> fig/test/

Use --overleaf-dir to have this script copy them into that layout directly.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PY = sys.executable
ALL = (16, 17, 18, 19, 20, 21, 22, 23)

OVERLEAF = {
    16: "fig/final-0609-figures/fig0609_placement_perfect.eps",
    17: "fig/final-0609-figures/fig0609_footprint_regimes.eps",
    18: "fig/final-0609-figures/fig0609_group_size_sensitivity.eps",
    19: "fig/test/fig_repro_s53_policy.eps",
    20: "fig/test/fig_repro_s53_invblk.eps",
    21: "fig/test/fig_repro_s54_degree.eps",
    22: "fig/test/fig_repro_s54_capacity.eps",
    23: "fig/test/fig_repro_s54_ratio.eps",
}

PRODUCED = {
    16: "fig0609_placement_perfect.eps",
    17: "fig0609_footprint_regimes.eps",
    18: "fig0609_group_size_sensitivity.eps",
    19: "fig_repro_s53_policy.eps",
    20: "fig_repro_s53_invblk.eps",
    21: "fig_repro_s54_degree.eps",
    22: "fig_repro_s54_capacity.eps",
    23: "fig_repro_s54_ratio.eps",
}


def call(args, env=None, label=""):
    print("$", " ".join(str(a) for a in args), flush=True)
    subprocess.run([str(a) for a in args], cwd=ROOT, env=env, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--figs", default=",".join(str(f) for f in ALL),
                        help="comma-separated subset of 16,...,23")
    parser.add_argument("--data-root", default="results/repro_fig16_23")
    parser.add_argument("--fig-root", default="figures-repro")
    parser.add_argument("--overleaf-dir", default=None,
                        help="copy EPS into fig/final-0609-figures and fig/test here")
    parser.add_argument("--skip-data", action="store_true")
    parser.add_argument("--quick", action="store_true")
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()

    figs = [int(x) for x in args.figs.split(",") if x.strip()]
    data = (ROOT / args.data_root).resolve()
    fig_root = (ROOT / args.fig_root).resolve()
    data.mkdir(parents=True, exist_ok=True)
    fig_root.mkdir(parents=True, exist_ok=True)

    if not args.skip_data:
        if any(f in (16, 18) for f in figs):
            call([PY, "AE-scripts/run_ae_extension_experiments.py",
                  "--sections", "1.1", "1.3", "--profiles", "full",
                  "--timeout", "1200" if not args.quick else "300",
                  "--out-root", str(data / "s52_ext"), "--fresh-summary"])
        if 17 in figs:
            env = dict(os.environ, REPRO_FOOTPRINT_ROOT=str(data / "s52_footprint"))
            call([PY, "AE-scripts/run_footprint_steady.py",
                  "--stride", "45", "--seeds", "10000,20000,30000",
                  "--timeout", "1200" if not args.quick else "300"],
                 env=env, label="footprint")
        if any(f in (19, 20) for f in figs):
            call([PY, "AE-scripts/run_s53_figures.py", "--out-root", str(data),
                  "--jobs", str(args.jobs), *(["--quick"] if args.quick else [])])
        if any(f in (21, 22, 23) for f in figs):
            call([PY, "AE-scripts/run_s54_figures.py",
                  "--out-root", str(data / "s54"), "--jobs", str(args.jobs),
                  *(["--quick"] if args.quick else [])])

    if any(f in (16, 18) for f in figs):
        call([PY, "output/plot_ae_figures_rev.py",
              "--ext-csv", str(data / "s52_ext" / "ae_extension_results.csv"),
              "--out-dir", str(fig_root), "--formats", "eps",
              "--only", "placement,group_size"])
    if 17 in figs:
        call([PY, "output/plot_fig17_steady_style.py",
              "--data-root", str(data / "s52_footprint"), "--out-dir", str(fig_root)])
    if any(f in (19, 20) for f in figs):
        only = ",".join(x for f, x in ((19, "policy"), (20, "invblk")) if f in figs)
        call([PY, "output/plot_s53_cln.py", "--data-root", str(data),
              "--out-dir", str(fig_root), "--only", only])
    if any(f in (21, 22, 23) for f in figs):
        only = ",".join(x for f, x in ((21, "degree"), (22, "capacity"), (23, "ratio"))
                        if f in figs)
        call([PY, "output/plot_repro_final.py",
              "--data-root", str(data / "s54"), "--cap-dir", str(data / "s54"),
              "--ratio-dir", str(data / "s54"), "--out-dir", str(fig_root),
              "--only", only])

    missing = [PRODUCED[f] for f in figs if not (fig_root / PRODUCED[f]).is_file()]
    if missing:
        print("\nmissing outputs:\n  " + "\n  ".join(missing), file=sys.stderr)
        return 1
    print("\nfigures written to", fig_root)
    print("Overleaf destinations:")
    for f in figs:
        print(f"  {PRODUCED[f]:36s} -> {OVERLEAF[f]}")
        if args.overleaf_dir:
            dst = Path(args.overleaf_dir) / OVERLEAF[f]
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(fig_root / PRODUCED[f], dst)
    if args.overleaf_dir:
        print("copied into", Path(args.overleaf_dir))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
