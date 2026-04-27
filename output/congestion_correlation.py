import csv
import math
import argparse
import sys
from collections import defaultdict

parser = argparse.ArgumentParser(
    description='Compute cross-correlation between eviction events and queue depth')
parser.add_argument("--qdepth", required=True, help="Switch queue depth CSV")
parser.add_argument("--eviction", required=True, help="Snoop eviction CSV")
parser.add_argument("--window", type=int, default=5000,
                    help="Analysis window in ns")
parser.add_argument("--threshold", type=int, default=8,
                    help="Queue depth threshold")
args = parser.parse_args()


def read_csv(filename):
    rows = []
    try:
        with open(filename) as f:
            reader = csv.DictReader(f)
            for row in reader:
                rows.append(row)
    except Exception as e:
        print(f"Error reading {filename}: {e}")
    return rows


def pearson_corr(xs, ys):
    n = len(xs)
    if n < 3:
        return float('nan')
    sum_x = sum(xs)
    sum_y = sum(ys)
    sum_xy = sum(x * y for x, y in zip(xs, ys))
    sum_x2 = sum(x * x for x in xs)
    sum_y2 = sum(y * y for y in ys)
    num = n * sum_xy - sum_x * sum_y
    den = math.sqrt((n * sum_x2 - sum_x * sum_x) *
                    (n * sum_y2 - sum_y * sum_y))
    if den == 0:
        return float('nan')
    return num / den


qd_rows = read_csv(args.qdepth)
ev_rows = read_csv(args.eviction)

if not qd_rows or not ev_rows:
    print("One or both CSV files are empty. No correlation to compute.")
    sys.exit(0)

# Build per-window per-port max queue depth
qd_windows = defaultdict(lambda: defaultdict(float))
for row in qd_rows:
    ts = int(row.get('timestamp', 0))
    port = int(row.get('port', 0))
    depth = float(row.get('queue_depth', 0))
    w = (ts // args.window) * args.window
    qd_windows[w][port] = max(qd_windows[w].get(port, 0), depth)

# Build per-window per-target_host inv count
ev_windows = defaultdict(lambda: defaultdict(int))
for row in ev_rows:
    ts = int(row.get('timestamp', 0))
    host = int(row.get('target_host', 0))
    w = (ts // args.window) * args.window
    ev_windows[w][host] += 1

# Collect all windows and ports/hosts
all_windows = sorted(set(list(qd_windows.keys()) + list(ev_windows.keys())))
all_ports = set()
for wdata in qd_windows.values():
    all_ports.update(wdata.keys())
all_hosts = set()
for wdata in ev_windows.values():
    all_hosts.update(wdata.keys())

if not all_windows:
    print("No aligned time windows found. No correlation to compute.")
    sys.exit(0)

# Build aligned series
def build_series(windows, data_dict, key_func, default=0.0):
    series = []
    for w in windows:
        d = data_dict.get(w, {})
        series.append(key_func(d))
    return series

results = []
max_lag = 20

for snoop_host in all_hosts:
    for sw_port in all_ports:
        ev_vals = [ev_windows.get(w, {}).get(snoop_host, 0) for w in all_windows]
        qd_vals = [qd_windows.get(w, {}).get(sw_port, 0) for w in all_windows]

        n = len(ev_vals)
        if n < 3:
            continue

        max_corr = -1
        best_lag = 0
        for lag in range(-max_lag, max_lag + 1):
            if lag < 0:
                lag_abs = -lag
                corr = pearson_corr(ev_vals[lag_abs:], qd_vals[:(-lag_abs) if -lag_abs != 0 else n])
            elif lag > 0:
                corr = pearson_corr(ev_vals[:-lag if lag != n else None], qd_vals[lag:])
            else:
                corr = pearson_corr(ev_vals, qd_vals)

            if not math.isnan(corr) and abs(corr) > abs(max_corr):
                max_corr = corr
                best_lag = lag

        if not math.isnan(max_corr):
            results.append({
                'snoop_host': snoop_host,
                'switch_port': sw_port,
                'max_correlation': round(max_corr, 4),
                'best_lag_us': round(best_lag * args.window / 1000, 2)
            })

if results:
    results.sort(key=lambda r: r['max_correlation'], reverse=True)
    max_snoop = max(r['snoop_host'] for r in results)
    max_port = max(r['switch_port'] for r in results)
    sh_w = max(len(str(max_snoop)), 10)
    sp_w = max(len(str(max_port)), 11)
    print(f"{'snoop_host':>{sh_w}}  {'switch_port':>{sp_w}}  {'max_correlation':>16}  {'best_lag_us':>12}")
    for r in results:
        print(f"{r['snoop_host']:>{sh_w}}  {r['switch_port']:>{sp_w}}  "
              f"{r['max_correlation']:>16}  {r['best_lag_us']:>12}")
else:
    print("No correlation results computed (insufficient data).")
