#!/usr/bin/env python3
"""Recompute every number in revision/findings-5.3.md from the CSVs.

Read-only: this script never runs a simulation and never writes a file.  It
prints the tables in the same order as the findings document; every table says
which CSV it came from, and row-level tables carry a 'src' column.

Usage (from the Xerxes repository root):

    python3 AE-scripts/fit53.py

Paths are resolved relative to this file, so running it from elsewhere works
too.  Only the Python standard library is used.

Datasets
--------
fig13_close/summary.csv              baseline (group a), burst (group b), seeds
fig13_sigma/summary.csv              sigma x clean scan (wr = .25/.5/.75)
fig13_sweep/summary.csv              wr scan at sigma = 3280/512 = 6.40625
fig13_a43_wr_endpoints/summary.csv   wr = 0/1 endpoints at sigma = 1/4
fig13_a44_random/summary.csv         Random as the sixth policy
"""

import csv
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]          # .../Xerxes
CLOSE = "output/fig13_close/summary.csv"
SIGMA = "output/fig13_sigma/summary.csv"
SWEEP = "output/fig13_sweep/summary.csv"
A43 = "output/fig13_a43_wr_endpoints/summary.csv"
A44 = "output/fig13_a44_random/summary.csv"

POL5 = ["FIFO", "LRU", "LFI", "LIFO", "MRU"]
POL5_DOC = ["FIFO", "LRU", "MRU", "LIFO", "LFI"]
WRS3 = [0.25, 0.50, 0.75]
WRS5 = [0.0, 0.25, 0.50, 0.75, 1.0]
N_REQ = 30000.0


# ----------------------------------------------------------------------------
# IO / numerics
# ----------------------------------------------------------------------------
def load(rel):
    path = ROOT / rel
    if not path.exists():
        sys.exit("missing input: %s" % path)
    with path.open(newline="") as fh:
        return list(csv.DictReader(fh))


def num(row, key):
    return float(row[key])


def fit_line(pts):
    """Ordinary least squares.

    pts = [(x, y), ...] -> (intercept, slope, r2, residual_sd).
    """
    n = len(pts)
    mx = sum(x for x, _ in pts) / n
    my = sum(y for _, y in pts) / n
    den = sum((x - mx) ** 2 for x, _ in pts)
    if den == 0.0:
        return my, 0.0, 1.0, 0.0
    slope = sum((x - mx) * (y - my) for x, y in pts) / den
    intercept = my - slope * mx
    ss_tot = sum((y - my) ** 2 for _, y in pts)
    ss_res = sum((y - (intercept + slope * x)) ** 2 for x, y in pts)
    r2 = 1.0 - ss_res / ss_tot if ss_tot else 1.0
    sd = math.sqrt(ss_res / max(n - 2, 1))
    return intercept, slope, r2, sd


def joint_fit(obs):
    """Shared slope, one intercept per wr (the A4-3 estimator).

    obs = {wr: [(I, latency), ...]}
    Returns (L0 dict, k per 1k invalidations, residual_sd, se_k_per_inv, means).
    """
    wrs = sorted(obs)
    num_ = den_ = 0.0
    means = {}
    for w in wrs:
        pts = obs[w]
        mx = sum(x for x, _ in pts) / len(pts)
        my = sum(y for _, y in pts) / len(pts)
        means[w] = (mx, my)
        num_ += sum((x - mx) * (y - my) for x, y in pts)
        den_ += sum((x - mx) ** 2 for x, y in pts)
    k = num_ / den_
    L0 = {w: means[w][1] - k * means[w][0] for w in wrs}
    res = [y - (L0[w] + k * x) for w in wrs for x, y in obs[w]]
    dof = max(len(res) - len(wrs) - 1, 1)
    sd = math.sqrt(sum(r * r for r in res) / dof)
    se_k = sd / math.sqrt(den_) if den_ else 0.0
    return L0, k * 1000.0, sd, se_k, means


def spread(vals):
    return max(vals) - min(vals)


# ----------------------------------------------------------------------------
# Main
# ----------------------------------------------------------------------------
def main():
    close = load(CLOSE)
    sigma = load(SIGMA)
    sweep = load(SWEEP)
    a43 = load(A43)
    a44 = load(A44)

    print("=" * 100)
    print("Section 5.3 number recomputation (read-only; source CSVs under Xerxes/output/)")
    print("=" * 100)
    print("input rows:")
    print("  %-34s %4d rows" % (CLOSE, len(close)))
    print("  %-34s %4d rows" % (SIGMA, len(sigma)))
    print("  %-34s %4d rows" % (SWEEP, len(sweep)))
    print("  %-34s %4d rows" % (A43, len(a43)))
    print("  %-34s %4d rows" % (A44, len(a44)))

    # ------------------------------------------------------------------
    # [1] baseline (findings section 3)
    # ------------------------------------------------------------------
    base = [r for r in close
            if r["group"] == "a" and r["notify"] == "False"]
    base.sort(key=lambda r: POL5_DOC.index(r["policy"]))
    fifo = [r for r in base if r["policy"] == "FIFO"][0]
    print("\n" + "=" * 100)
    print("[1] baseline, group a (findings section 3)   source: %s  rows group=a notify=off n=%d"
          % (CLOSE, len(base)))
    print("=" * 100)
    print("%-6s %8s %8s %7s %9s %9s  %s"
          % ("policy", "inv", "hit", "hit%", "lat_ns", "bw_GB/s", "src"))
    for r in base:
        hit = num(r, "hit")
        req = num(r, "req")
        print("%-6s %8.0f %8.0f %6.2f%% %9.3f %9.5f  %s"
              % (r["policy"], num(r, "bisnp"), hit, 100.0 * hit / (hit + req),
                 num(r, "avg_latency_ns"), num(r, "bw_gbps"), CLOSE))
    print("\nrelative to FIFO (LIFO vs FIFO = the finding's +2.0% / -17.7% / -47.2%):")
    for r in base:
        print("  %-4s  bw %+6.2f%%   lat %+7.2f%%   inval %+7.2f%%   (hit %+6.2f%%)  src=%s"
              % (r["policy"],
                 100.0 * (num(r, "bw_gbps") / num(fifo, "bw_gbps") - 1.0),
                 100.0 * (num(r, "avg_latency_ns") / num(fifo, "avg_latency_ns") - 1.0),
                 100.0 * (num(r, "bisnp") / num(fifo, "bisnp") - 1.0),
                 100.0 * (num(r, "hit") / num(fifo, "hit") - 1.0), CLOSE))

    pts_hit = [(num(r, "bisnp"), num(r, "hit")) for r in base]
    pts_lat = [(num(r, "bisnp"), num(r, "avg_latency_ns")) for r in base]
    a_hit, b_hit, r2_hit, _ = fit_line(pts_hit)
    a_lat, b_lat, r2_lat, _ = fit_line(pts_lat)
    print("\n  5-policy regressions over group a (finding section 7):")
    print("  hit  = %9.3f %+9.5f * I   R2=%.4f   src=%s"
          % (a_hit, b_hit, r2_hit, CLOSE))
    print("  lat  = %9.4f %+9.6f * I   R2=%.4f   src=%s"
          % (a_lat, b_lat, r2_lat, CLOSE))

    # ------------------------------------------------------------------
    # [2] lat ~ I per (sigma, wr) (findings section 4.2, item 1 of the job)
    # ------------------------------------------------------------------
    off_sigma = [r for r in sigma if r["clean"] == "off"]
    on_sigma = [r for r in sigma if r["clean"] == "on"]
    by_key = {}
    for r in off_sigma:
        by_key.setdefault((int(r["sf"]), float(r["wr"])), []).append(r)
    on_key = {}
    for r in on_sigma:
        on_key.setdefault((int(r["sf"]), float(r["wr"])), {})[r["policy"]] = r

    print("\n" + "=" * 100)
    print("[2] lat ~ I free fit per (sigma, wr), five policies (findings section 4.2)")
    print("    source: %s  rows clean=off" % SIGMA)
    print("=" * 100)
    print("%5s %5s %5s %10s %10s %7s %8s %7s  %s"
          % ("sigma", "wr", "n", "L0_ns", "k_ns/1k", "R2", "resid_sd", "I_range", "src"))
    all_r2 = []
    for sf in sorted({int(r["sf"]) for r in off_sigma}):
        for wr in WRS3:
            rows = by_key[(sf, wr)]
            pts = [(num(r, "total_inv_packets"), num(r, "avg_latency_ns")) for r in rows]
            L0, k, r2, sd = fit_line(pts)
            all_r2.append(r2)
            ir = [x for x, _ in pts]
            print("%5.2f %5.2f %5d %10.3f %10.3f %7.4f %8.3f %7.0f  %s"
                  % (sf / 512.0, wr, len(pts), L0, k * 1000.0, r2, sd,
                     max(ir) - min(ir), SIGMA))

    sweep_off = [r for r in sweep
                 if r["clean"] == "off" and r["tag"].startswith("r")
                 and not r["tag"].startswith("ref")]
    sw_sigma = num(sweep_off[0], "sf") / num(sweep_off[0], "cache")
    print("\n    sigma=6.4 reference (five policies, five wr incl. endpoints):")
    print("    source: %s  rows clean=off tag^=r" % SWEEP)
    sweep_by_wr = {}
    for r in sweep_off:
        sweep_by_wr.setdefault(r["wr"], []).append(r)
    for tag in ("r000", "r025", "r050", "r075", "r100"):
        rows = sweep_by_wr[tag]
        pts = [(num(r, "total_inv_packets"), num(r, "avg_latency_ns")) for r in rows]
        L0, k, r2, sd = fit_line(pts)
        print("%5.2f %5s %5d %10.3f %10.3f %7.4f %8.3f %7.0f  %s"
              % (sw_sigma, tag, len(pts), L0, k * 1000.0, r2, sd,
                 max(x for x, _ in pts) - min(x for x, _ in pts), SWEEP))
    print("\n    18 sigma-grid fits: R2 range %.4f .. %.4f" % (min(all_r2), max(all_r2)))

    # ------------------------------------------------------------------
    # [3] b(sigma) and k(sigma) bins (findings section 4.1, item 2)
    # ------------------------------------------------------------------
    print("\n" + "=" * 100)
    print("[3] b(sigma) = -d(hit)/d(I) and k(sigma) = d(lat)/d(I), bins (findings section 4.1)")
    print("    source: %s (sigma grid), %s (sigma=6.4); rows clean=off" % (SIGMA, SWEEP))
    print("=" * 100)
    print("%5s %5s %9s %7s %9s %7s  %s"
          % ("sigma", "wr", "b", "R2", "k_ns/1k", "R2", "src"))
    b_bins = {}
    k_bins = {}
    for sf in sorted({int(r["sf"]) for r in off_sigma}):
        s = sf / 512.0
        b_bins[s] = []
        k_bins[s] = []
        for wr in WRS3:
            rows = by_key[(sf, wr)]
            pts_b = [(num(r, "total_inv_packets"), num(r, "cache_hit_count")) for r in rows]
            pts_k = [(num(r, "total_inv_packets"), num(r, "avg_latency_ns")) for r in rows]
            _, slope_b, r2_b, _ = fit_line(pts_b)
            _, slope_k, r2_k, _ = fit_line(pts_k)
            b = -slope_b
            k = slope_k * 1000.0
            b_bins[s].append((b, r2_b))
            k_bins[s].append(k)
            print("%5.2f %5.2f %9.4f %7.4f %9.3f %7.4f  %s"
                  % (s, wr, b, r2_b, k, r2_k, SIGMA))
    print("\n    sigma=6.4 reference (wr=.25/.5/.75):")
    for tag, wr in (("r025", 0.25), ("r050", 0.50), ("r075", 0.75)):
        rows = sweep_by_wr[tag]
        pts_b = [(num(r, "total_inv_packets"), num(r, "cache_hit_count")) for r in rows]
        pts_k = [(num(r, "total_inv_packets"), num(r, "avg_latency_ns")) for r in rows]
        _, slope_b, r2_b, _ = fit_line(pts_b)
        _, slope_k, r2_k, _ = fit_line(pts_k)
        b = -slope_b
        k = slope_k * 1000.0
        b_bins.setdefault(sw_sigma, []).append((b, r2_b))
        k_bins.setdefault(sw_sigma, []).append(k)
        print("%5.2f %5.2f %9.4f %7.4f %9.3f %7.4f  %s"
              % (sw_sigma, wr, b, r2_b, k, r2_k, SWEEP))

    print("\n    per-sigma bin summary (mean / min / max over the three inner wr):")
    print("%6s %22s %22s %20s  %s"
          % ("sigma", "b mean [min,max]", "b R2 mean [min,max]", "k mean [min,max]", "src"))
    for s in sorted(b_bins):
        bs = [x[0] for x in b_bins[s]]
        rs = [x[1] for x in b_bins[s]]
        ks = k_bins[s]
        print("%6.2f %10.4f [%7.4f,%7.4f] %8.4f [%6.4f,%6.4f] %9.3f [%6.3f,%6.3f]  %s"
              % (s, sum(bs) / len(bs), min(bs), max(bs),
                 sum(rs) / len(rs), min(rs), max(rs),
                 sum(ks) / len(ks), min(ks), max(ks),
                 SIGMA if s != sw_sigma else SWEEP))
    print("\n    model decomposition used by the finding, k = (b*161 + s)/30 for N=30,000:")
    print("    (b*161/30 is the pure hit-destruction term; s_eff = 30*k - 161*b)")
    print("%6s %9s %9s %11s %10s  %s"
          % ("sigma", "b_mean", "k_mean", "b*161/30", "s_eff_ns", "src"))
    for s in sorted(b_bins):
        bm = sum(x[0] for x in b_bins[s]) / len(b_bins[s])
        km = sum(k_bins[s]) / len(k_bins[s])
        print("%6.2f %9.4f %9.3f %11.3f %10.2f  %s"
              % (s, bm, km, bm * 161.0 / 30.0, 30.0 * km - 161.0 * bm,
                 SIGMA if s != sw_sigma else SWEEP))

    # ------------------------------------------------------------------
    # [4] Delta(wr): five points at three sigma (findings section 5, item 3)
    # ------------------------------------------------------------------
    print("\n" + "=" * 100)
    print("[4] Delta(wr): five points at sigma = 1 / 4 / 6.4 (findings section 5)")
    print("    sigma=1,4: %s (wr 0/1) + %s (wr .25/.5/.75), 4 common policies" % (A43, SIGMA))
    print("    sigma=6.4: %s (five wr, five policies)" % SWEEP)
    print("=" * 100)

    a43_off = [r for r in a43 if r["clean"] == "off"]
    delta = {}
    for s, sf in ((1.0, 512), (4.0, 2048)):
        obs = {}
        for wr in WRS5:
            pts = []
            for p in ["FIFO", "LRU", "LFI", "LIFO"]:
                if wr in (0.0, 1.0):
                    row = [r for r in a43_off
                           if int(r["sf"]) == sf and float(r["wr"]) == wr
                           and r["policy"] == p][0]
                else:
                    row = [r for r in off_sigma
                           if int(r["sf"]) == sf and abs(float(r["wr"]) - wr) < 1e-9
                           and r["policy"] == p][0]
                pts.append((num(row, "total_inv_packets"),
                            num(row, "avg_latency_ns")))
            obs[wr] = pts
        L0, k, sd, se_k, means = joint_fit(obs)
        d = {w: L0[w] - L0[0.0] for w in WRS5}
        se_d = {w: se_k * abs(means[w][0] - means[0.0][0]) for w in WRS5}
        delta[s] = d
        print("\n  sigma=%.1f  shared k=%.3f ns/1k  residual sd=%.3f ns" % (s, k, sd))
        print("  %5s %10s %10s %12s %10s %10s %9s"
              % ("wr", "L0_ns", "Delta_ns", "se(Delta)", "mean_I", "free_L0", "free_k"))
        for w in WRS5:
            free_L0, free_k, _, _ = fit_line(obs[w])
            print("  %5.2f %10.3f %+10.3f %12.4f %10.1f %10.2f %9.3f"
                  % (w, L0[w], d[w], se_d[w], means[w][0], free_L0, free_k * 1000.0))

    obs = {}
    for wr, tag in zip(WRS5, ("r000", "r025", "r050", "r075", "r100")):
        pts = []
        for p in POL5:
            row = [r for r in sweep if r["wr"] == tag and r["policy"] == p][0]
            pts.append((num(row, "total_inv_packets"), num(row, "avg_latency_ns")))
        obs[wr] = pts
    L0, k64, sd64, se_k64, means64 = joint_fit(obs)
    d64 = {w: L0[w] - L0[0.0] for w in WRS5}
    delta[sw_sigma] = d64
    print("\n  sigma=6.4  shared k=%.3f ns/1k  residual sd=%.3f ns" % (k64, sd64))
    print("  %5s %10s %10s %12s %10s"
          % ("wr", "L0_ns", "Delta_ns", "se(Delta)", "mean_I"))
    for w in WRS5:
        se_d = se_k64 * abs(means64[w][0] - means64[0.0][0])
        print("  %5.2f %10.3f %+10.3f %12.4f %10.1f"
              % (w, L0[w], d64[w], se_d, means64[w][0]))

    print("\n  second differences of Delta(wr) (ns):")
    print("  %6s %9s %9s %9s  %s"
          % ("sigma", "d2(.25)", "d2(.5)", "d2(.75)", "src"))
    for s in (1.0, 4.0, sw_sigma):
        dv = [delta[s][w] for w in WRS5]
        d1 = [dv[i + 1] - dv[i] for i in range(4)]
        d2 = [d1[i + 1] - d1[i] for i in range(3)]
        print("  %6.2f %+9.3f %+9.3f %+9.3f  %s"
              % (s, d2[0], d2[1], d2[2], A43 if s != sw_sigma else SWEEP))

    print("\n  cross-sigma spread of the same Delta point (three sigma):")
    print("  %6s %9s %9s %9s %9s  %s"
          % ("wr", "sigma=1", "sigma=4", "sigma=6.4", "spread", "src"))
    for w in WRS5[1:]:
        vals = [delta[1.0][w], delta[4.0][w], delta[sw_sigma][w]]
        print("  %6.2f %+9.3f %+9.3f %+9.3f %9.3f  %s+%s+%s"
              % (w, vals[0], vals[1], vals[2], spread(vals), A43, SIGMA, SWEEP))

    print("\n  cross-policy spread of Delta(wr) at sigma=6.4 (finding says <=0.7 ns):")
    print("  %6s %10s  %s" % ("wr", "spread_ns", "policies FIFO/LRU/LFI/LIFO/MRU"))
    for tag, w in (("r025", 0.25), ("r050", 0.50), ("r075", 0.75), ("r100", 1.00)):
        ds = []
        for p in POL5:
            r0 = [r for r in sweep if r["wr"] == "r000" and r["policy"] == p][0]
            rw = [r for r in sweep if r["wr"] == tag and r["policy"] == p][0]
            raw = num(rw, "avg_latency_ns") - num(r0, "avg_latency_ns")
            corr = k64 / 1000.0 * (num(rw, "total_inv_packets")
                                   - num(r0, "total_inv_packets"))
            ds.append(raw - corr)
        print("  %6.2f %10.3f  %s" % (w, spread(ds),
                                      " ".join("%+.2f" % x for x in ds)))

    print("\n  sigma=1 free-fit L0 across the five wr (finding says 42-64 ns):")
    free_l0 = []
    for w in WRS5:
        pts = []
        for p in ["FIFO", "LRU", "LFI", "LIFO"]:
            if w in (0.0, 1.0):
                row = [r for r in a43_off if int(r["sf"]) == 512
                       and float(r["wr"]) == w and r["policy"] == p][0]
            else:
                row = [r for r in off_sigma if int(r["sf"]) == 512
                       and abs(float(r["wr"]) - w) < 1e-9 and r["policy"] == p][0]
            pts.append((num(row, "total_inv_packets"),
                        num(row, "avg_latency_ns")))
        L0f, kf, r2f, _ = fit_line(pts)
        free_l0.append(L0f)
        print("    wr=%.2f free_L0=%7.2f free_k=%6.3f R2=%.4f  src=%s+%s"
              % (w, L0f, kf * 1000.0, r2f, A43, SIGMA))
    print("    range = %.2f .. %.2f ns" % (min(free_l0), max(free_l0)))

    # ------------------------------------------------------------------
    # [5] off fit -> on prediction (findings section 4.2, item 4)
    # ------------------------------------------------------------------
    print("\n" + "=" * 100)
    print("[5] off-state line predicts on-state latency (findings section 4.2)")
    print("    source: %s  rows clean=off and clean=on" % SIGMA)
    print("=" * 100)
    print("%5s %5s %9s %9s %9s %9s %10s  %s"
          % ("sigma", "wr", "MAE_ns", "max_err", "mean_rel%", "max_rel%", "inv_cut%", "src"))
    mae_all, rel_all = [], []
    for sf in sorted({int(r["sf"]) for r in off_sigma}):
        for wr in WRS3:
            off = by_key[(sf, wr)]
            off_pts = [(num(r, "total_inv_packets"), num(r, "avg_latency_ns")) for r in off]
            a, b, _, _ = fit_line(off_pts)
            errs, rels, cuts = [], [], []
            for p in POL5:
                ro = [r for r in off if r["policy"] == p][0]
                rn = on_key[(sf, wr)][p]
                pred = a + b * num(rn, "total_inv_packets")
                actual = num(rn, "avg_latency_ns")
                errs.append(abs(pred - actual))
                rels.append(abs(pred - actual) / actual * 100.0)
                cuts.append(100.0 * (1.0 - num(rn, "total_inv_packets")
                                     / max(num(ro, "total_inv_packets"), 1.0)))
            mae = sum(errs) / len(errs)
            mrel = sum(rels) / len(rels)
            mae_all.append(mae)
            rel_all.append(mrel)
            print("%5.2f %5.2f %9.3f %9.3f %9.3f %9.3f %7.1f-%-5.1f  %s"
                  % (sf / 512.0, wr, mae, max(errs), mrel, max(rels),
                     min(cuts), max(cuts), SIGMA))
    print("\n  per-(sigma,wr) MAE range: %.3f .. %.3f ns" % (min(mae_all), max(mae_all)))
    print("  per-(sigma,wr) mean-relative range: %.3f%% .. %.3f%%"
          % (min(rel_all), max(rel_all)))

    # ------------------------------------------------------------------
    # [6] structural facts (findings section 4.3) + baseline amplitudes (item 5)
    # ------------------------------------------------------------------
    print("\n" + "=" * 100)
    print("[6] structural facts, LIFO at wr=0.5 (findings section 4.3)")
    print("    source: %s  (SF invalidation from clean=off; host self-eviction from clean=on)" % SIGMA)
    print("=" * 100)
    print("%5s %10s %10s %10s %10s %9s %9s  %s"
          % ("sigma", "SF_inv_off", "SF_inv_on", "host_sent", "host_rx",
             "rx/SF_off", "on_lat%", "src"))
    struct = {}
    for sf in (128, 512, 1024, 2048, 4096):
        s = sf / 512.0
        ro = [r for r in off_sigma if int(r["sf"]) == sf and r["wr"] == "0.5"
              and r["policy"] == "LIFO"][0]
        rn = [r for r in on_sigma if int(r["sf"]) == sf and r["wr"] == "0.5"
              and r["policy"] == "LIFO"][0]
        on_lat = [num(r, "avg_latency_ns") for r in on_sigma
                  if int(r["sf"]) == sf and r["wr"] == "0.5"]
        struct[s] = dict(inv_off=num(ro, "total_inv_packets"),
                         inv_on=num(rn, "total_inv_packets"),
                         sent=num(rn, "cln_evct_sent"),
                         rx=num(rn, "cln_evct_rx"))
        print("%5.2f %10.0f %10.0f %10.0f %10.0f %9.3f %8.2f%%  %s"
              % (s, struct[s]["inv_off"], struct[s]["inv_on"],
                 struct[s]["sent"], struct[s]["rx"],
                 struct[s]["rx"] / struct[s]["inv_off"],
                 100.0 * (max(on_lat) / min(on_lat) - 1.0), SIGMA))
    s1 = struct[1.0]
    print("\n  sigma=1: %.0f SF invalidations (off) vs %.0f host self-evictions (on); ratio %.2f%%"
          % (s1["inv_off"], s1["rx"], 100.0 * s1["rx"] / s1["inv_off"]))
    lo, hi = 2.0, 4.0
    rlo = struct[lo]["rx"] / struct[lo]["inv_off"]
    rhi = struct[hi]["rx"] / struct[hi]["inv_off"]
    if rlo < 1.0 < rhi:
        cross = lo * (hi / lo) ** (math.log(1.0 / rlo) / math.log(rhi / rlo))
        print("  rx/SF crosses 1 between sigma=2 (%.3f) and sigma=4 (%.3f): sigma ~ %.2f"
              % (rlo, rhi, cross))

    print("\n  baseline LIFO-vs-FIFO amplitudes (finding section 3):")
    lifo = [r for r in base if r["policy"] == "LIFO"][0]
    print("    bandwidth  %+6.2f%%   source=%s"
          % (100.0 * (num(lifo, "bw_gbps") / num(fifo, "bw_gbps") - 1.0), CLOSE))
    print("    latency    %+6.2f%%   source=%s"
          % (100.0 * (num(lifo, "avg_latency_ns") / num(fifo, "avg_latency_ns") - 1.0), CLOSE))
    print("    invalid.   %+6.2f%%   source=%s"
          % (100.0 * (num(lifo, "bisnp") / num(fifo, "bisnp") - 1.0), CLOSE))

    print("\n  sigma=6.4 invalidation stability across wr (finding section 5):")
    print("  %-6s %s  %s" % ("policy", "I(r000..r100)", "range"))
    for p in POL5:
        vals = [num([r for r in sweep if r["wr"] == tag and r["policy"] == p][0],
                    "total_inv_packets")
                for tag in ("r000", "r025", "r050", "r075", "r100")]
        print("  %-6s %s  %.0f  %s"
              % (p, "/".join("%.0f" % v for v in vals), spread(vals), SWEEP))

    # ------------------------------------------------------------------
    # [7] A4-3 endpoints (findings section 5 / 10.1, item 6)
    # ------------------------------------------------------------------
    print("\n" + "=" * 100)
    print("[7] A4-3 wr endpoints (findings section 5 / 10.1)")
    print("    source: %s  n=%d" % (A43, len(a43)))
    print("=" * 100)
    print("%5s %4s %-7s %10s %8s %9s %9s %8s  %s"
          % ("sigma", "wr", "policy", "lat_ns", "inv", "bw_GB/s", "hit", "proc", "src"))
    for r in sorted(a43, key=lambda r: (float(r["sigma"]), float(r["wr"]), r["policy"])):
        print("%5.2f %4.0f %-7s %10.3f %8.0f %9.5f %9.0f %8.0f  %s"
              % (num(r, "sigma"), num(r, "wr"), r["policy"],
                 num(r, "avg_latency_ns"), num(r, "total_inv_packets"),
                 num(r, "bw_gbps"), num(r, "cache_hit_count"),
                 num(r, "processed"), A43))

    audit_ok = all(num(r, "processed") == N_REQ for r in a43)
    unexp_ok = all(num(r, "unexpected_pkt") == 0 for r in a43)
    inv_ok = all(num(r, "total_inv_packets") == num(r, "back_inv_count") for r in a43)
    rx_ok = all(num(r, "cln_evct_rx") == 0 for r in a43)
    print("\n  audit: processed==30000 %s (%d/%d); unexpected_pkt==0 %s; "
          "inv==back_inv %s; off rx==0 %s"
          % (audit_ok, sum(1 for r in a43 if num(r, "processed") == N_REQ), len(a43),
             unexp_ok, inv_ok, rx_ok))

    print("\n  FS+LIFO vs LIFO paired cells:")
    pairs = 0
    identical = 0
    for r in [q for q in a43 if q["policy"] == "FS+LIFO"]:
        twin_tag = r["tag"].replace("FS+LIFO", "LIFO")
        twin = [q for q in a43 if q["tag"] == twin_tag and q["policy"] == "LIFO"][0]
        pairs += 1
        same = all(abs(num(r, k) - num(twin, k)) < 1e-9
                   for k in ("avg_latency_ns", "total_inv_packets",
                             "cache_hit_count", "cache_clean_evict_count",
                             "cache_dirty_evict_count", "req_count"))
        identical += same
        print("    %-20s lat %.3f/% .3f inv %.0f/%.0f hit %.0f/%.0f identical=%s  %s"
              % (r["tag"], num(r, "avg_latency_ns"), num(twin, "avg_latency_ns"),
                 num(r, "total_inv_packets"), num(twin, "total_inv_packets"),
                 num(r, "cache_hit_count"), num(twin, "cache_hit_count"), same, A43))
    print("    identical pairs: %d/%d (A4-3 therefore has 4 independent policy curves)"
          % (identical, pairs))

    print("\n  endpoint invalidations quoted in the finding:")
    for s, sf in ((1.0, 512), (4.0, 2048)):
        for p in ("FIFO", "LIFO"):
            vals = []
            for w in (0.0, 1.0):
                r = [q for q in a43_off if int(q["sf"]) == sf
                     and float(q["wr"]) == w and q["policy"] == p][0]
                vals.append(num(r, "total_inv_packets"))
            print("    sigma=%.0f %-4s wr=0/1: %.0f / %.0f  %s"
                  % (s, p, vals[0], vals[1], A43))

    # ------------------------------------------------------------------
    # [8] A4-4 Random (findings section 10.2, item 6)
    # ------------------------------------------------------------------
    print("\n" + "=" * 100)
    print("[8] A4-4 Random as the sixth policy (findings section 10.2)")
    print("    source: %s (+ %s for the five-policy fallback)" % (A44, SIGMA))
    print("=" * 100)
    old05 = [r for r in off_sigma if abs(float(r["wr"]) - 0.5) < 1e-9]
    old_by_tag = {r["tag"]: r for r in old05}
    print("%5s %9s %9s %8s %9s %8s %8s  %s"
          % ("sigma", "lat_LIFO", "lat_Rand", "d_lat%", "inv_LIFO", "inv_Rand", "d_inv%", "src"))
    for sf in (128, 256, 512, 1024, 2048, 4096):
        rl = [r for r in a44 if int(r["sf"]) == sf and r["policy"] == "LIFO"][0]
        rr = [r for r in a44 if int(r["sf"]) == sf and r["policy"] == "Random"][0]
        print("%5.2f %9.3f %9.3f %+8.2f %9.0f %8.0f %+8.2f  %s"
              % (sf / 512.0, num(rl, "avg_latency_ns"), num(rr, "avg_latency_ns"),
                 100.0 * (num(rr, "avg_latency_ns") / num(rl, "avg_latency_ns") - 1.0),
                 num(rl, "total_inv_packets"), num(rr, "total_inv_packets"),
                 100.0 * (num(rr, "total_inv_packets") / num(rl, "total_inv_packets") - 1.0),
                 A44))

    print("\n  Random against the five-policy lat ~ I line (finding section 10.2):")
    print("%5s %8s %8s %7s %8s %10s %10s %8s %9s %8s %8s  %s"
          % ("sigma", "5p_L0", "5p_k", "5p_R2", "5p_sd", "lat_Rand", "pred",
             "resid", "resid/sd", "6p_R2", "6p_sd", "ranks"))
    resid_all = []
    for sf in (128, 256, 512, 1024, 2048, 4096):
        cells = {}
        pts5 = []
        for p in POL5:
            cand = [q for q in a44 if int(q["sf"]) == sf and q["policy"] == p]
            r = cand[0] if cand else old_by_tag["%d_050_off_%s" % (sf, p)]
            cells[p] = (num(r, "avg_latency_ns"), num(r, "total_inv_packets"))
            pts5.append((cells[p][1], cells[p][0]))
        rr = [q for q in a44 if int(q["sf"]) == sf and q["policy"] == "Random"][0]
        cr = (num(rr, "avg_latency_ns"), num(rr, "total_inv_packets"))
        cells["Random"] = cr
        a5, b5, r25, sd5 = fit_line(pts5)
        pred = a5 + b5 * cr[1]
        resid = cr[0] - pred
        resid_all.append((sf / 512.0, resid, cr[0]))
        a6, b6, r26, sd6 = fit_line(pts5 + [(cr[1], cr[0])])
        lat_rank = sorted(cells, key=lambda p: cells[p][0]).index("Random") + 1
        inv_rank = sorted(cells, key=lambda p: cells[p][1]).index("Random") + 1
        print("%5.2f %8.2f %8.3f %7.4f %8.3f %10.3f %10.3f %+8.3f %9.2f %8.4f %8.3f  %d/%d  %s"
              % (sf / 512.0, a5, b5 * 1000.0, r25, sd5, cr[0], pred, resid,
                 resid / sd5 if sd5 else 0.0, r26, sd6, lat_rank, inv_rank, A44))
    resids = [r for _, r, _ in resid_all]
    rel_max = max(abs(r) / lat * 100.0 for _, r, lat in resid_all)
    print("\n  residual range %+.3f .. %+.3f ns; mean |resid| %.3f ns; max relative %.2f%%"
          % (min(resids), max(resids), sum(abs(x) for x in resids) / len(resids), rel_max))

    print("\n  cross-commit replication of the 14 non-Random A4-4 cells against %s:" % SIGMA)
    rep = 0
    tot = 0
    for r in a44:
        if r["policy"] == "Random":
            continue
        cand = [q for q in old05 if int(q["sf"]) == int(r["sf"])
                and q["policy"] == r["policy"]
                and abs(float(q["wr"]) - float(r["wr"])) < 1e-9]
        if not cand:
            continue
        q = cand[0]
        tot += 1
        same = (abs(num(q, "avg_latency_ns") - num(r, "avg_latency_ns")) < 1e-9
                and num(q, "total_inv_packets") == num(r, "total_inv_packets")
                and num(q, "cache_hit_count") == num(r, "cache_hit_count"))
        rep += same
        if not same:
            print("    DIFF %s vs %s" % (r["tag"], q["tag"]))
    print("    identical: %d/%d  (d_lat = 0.0000 ns, d_inv = 0, d_hit = 0)" % (rep, tot))

    # ------------------------------------------------------------------
    # [9] InvBlk (findings section 6)
    # ------------------------------------------------------------------
    print("\n" + "=" * 100)
    print("[9] InvBlk / burst dimension (findings section 6)")
    print("    source: %s  rows group=b notify=off (2 hosts)" % CLOSE)
    print("=" * 100)
    burst = [r for r in close if r["group"] == "b" and r["notify"] == "False"]
    burst.sort(key=lambda r: int(r["burst"]))
    print("%5s %8s %10s %10s %8s  %s"
          % ("burst", "inv", "lat_ns", "bw_GB/s", "hosts", "src"))
    for r in burst:
        print("%5d %8.0f %10.3f %10.4f %8s  %s"
              % (int(r["burst"]), num(r, "bisnp"), num(r, "avg_latency_ns"),
                 num(r, "bw_gbps"), r["n_hosts"], CLOSE))

    # ------------------------------------------------------------------
    # [10] counts / checklist cross-checks
    # ------------------------------------------------------------------
    print("\n" + "=" * 100)
    print("[10] counts and audit cross-checks")
    print("=" * 100)
    print("  fig13_sigma rows: %d = 6 sigma x 3 wr x 2 clean x 5 policies" % len(sigma))
    print("  fig13_sweep rows: %d = 5 wr x 2 clean x 5 policies + 2 reference" % len(sweep))
    print("  A4-3 rows: %d = 2 sigma x 2 wr x 5 policies" % len(a43))
    print("  A4-4 rows: %d = 6 sigma x 2 policies + 2 sigma x 4 policies" % len(a44))
    print("  A4-4 non-Random rows: %d" % sum(1 for r in a44 if r["policy"] != "Random"))
    print("  group a req+hit == 30000: %s"
          % all(num(r, "req") + num(r, "hit") == N_REQ for r in base))
    print("  group a evict_events == bisnp: %s"
          % all(num(r, "evict_events") == num(r, "bisnp") for r in base))
    print("  group a clean == dirty == sent == rx == 0: %s"
          % all(num(r, "clean") == 0 and num(r, "dirty") == 0
                and num(r, "sent") == 0 and num(r, "rx") == 0 for r in base))
    fan = [num(r, "bisnp") / num(r, "back_inv") for r in close
           if num(r, "back_inv") > 0]
    print("  fan-out total_inv/back_inv over fig13_close: %.4f .. %.4f" % (min(fan), max(fan)))

    print("\nDONE.  All figures above are read-only recomputations from the CSVs.")
    print("=" * 100)


if __name__ == "__main__":
    main()
