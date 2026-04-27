# calculate the average latency and throughput
# the avg latency ar grouped by hop numbers
# hop number is switch_time / 4

import os
import argparse

WARMUP = 1000

total_count = 0
rw_iter = []
hop_base = 0
hop_count = {}
avg_switch_time = {}
avg_dram_time = {}
avg_latency = {} # ns
agg_avg_latency = 0 # ns
final_bw = 0 # MB/s
first_sent = 0 # ns
last_arrival = 0 # ns

# receive the file name from the user
parser = argparse.ArgumentParser()
parser.add_argument("file", help="csv file name")
parser.add_argument("--report", type=str, help="report type")
parser.add_argument("--compare-baseline", type=str, help="baseline CSV to compare against")
args = parser.parse_args()

# Try to import pandas; if not available, only baseline comparison works
try:
    import pandas as pd
    HAS_PANDAS = True
except ImportError:
    HAS_PANDAS = False

if HAS_PANDAS:
    # read the csv file
    file = args.file
    if not os.path.exists(file):
        print("File not found")
        exit(1)
    numeric_cols = [
        'switch_time', 'send', 'arrive',
        'switch_queuing', 'dram_queuing', 'total_time',
        'snoop_evict'
    ]

    # Read CSV robustly to avoid mixed-type NaNs from chunked inference
    try:
        dtype_map = {c: 'float64' for c in numeric_cols}
        dtype_map['type'] = 'string'
        data = pd.read_csv(file, dtype=dtype_map, low_memory=False)
    except Exception:
        # Fallback if headers differ; disable low_memory to stabilize dtypes
        data = pd.read_csv(file, low_memory=False)

    # Coerce numeric columns; keep only rows with valid numeric values where needed
    for c in numeric_cols:
        if c in data.columns:
            data[c] = pd.to_numeric(data[c], errors='coerce')
    if 'type' in data.columns:
        data['type'] = data['type'].astype('string')

    # calculate the average latency and throughput
    for index, row in data.iterrows():
        # Skip rows with invalid numeric fields to avoid NaNs propagating
        if any((col not in row or pd.isna(row[col])) for col in numeric_cols):
            continue
        if hop_base == 0:
            hop_base = row['switch_time']
        total_count += 1
        if index < WARMUP:
            # Still track aggregate latency for warmup as per original code
            agg_avg_latency += row['total_time']
            last_arrival = row['arrive']
            continue
        if first_sent == 0:
            first_sent = row['send']
        if args.report == "hoplat":
            hop = row['switch_time'] // hop_base
            if hop not in avg_latency:
                avg_latency[hop] = 0
                hop_count[hop] = 0
                avg_switch_time[hop] = 0
                avg_dram_time[hop] = 0
            hop_count[hop] += 1
            avg_switch_time[hop] += row['switch_queuing']
            avg_dram_time[hop] += row['dram_queuing']
            avg_latency[hop] += row['total_time']
        if args.report == "rw":
            if (index - WARMUP) % 3000 == 0:
                rw_iter.append([0, 0, -1, 0])
            if "read" in row['type'] or "Read" in row['type']:
                rw_iter[-1][0] += 1
            elif "write" in row['type'] or "Write" in row['type']:
                rw_iter[-1][1] += 1
            if rw_iter[-1][2] < 0: rw_iter[-1][2] = row['send']
            rw_iter[-1][3] = row['arrive']

        agg_avg_latency += row['total_time']
        last_arrival = row['arrive']

    for hop in avg_latency:
        avg_latency[hop] = avg_latency[hop] / hop_count[hop]
        avg_switch_time[hop] = avg_switch_time[hop] / hop_count[hop]
        avg_dram_time[hop] = avg_dram_time[hop] / hop_count[hop]

    final_avg_latency = agg_avg_latency / total_count
    final_bw = total_count * 64 / (last_arrival - first_sent) # B/ns
    final_bw = final_bw * 1e9 / (1024 * 1024) # MB/s
    avg_wait_inv = data['snoop_evict'].mean(skipna=True) if 'snoop_evict' in data.columns else 0

    if args.report == "hoplat":
        keys = list(avg_latency.keys())
        keys.sort()
        for hop in keys:
            print(f"{hop},{avg_latency[hop]}")
    elif args.report == "bw":
        print(f"{final_bw}")
    elif args.report == "rw":
        agg_read = 0
        agg_write = 0
        agg_start = -1
        agg_end = 0
        for it in rw_iter:
            if agg_start < 0: agg_start = it[2]
            if agg_end < it[3]: agg_end = it[3]
            read_count = it[0]
            write_count = it[1]
            total_time = it[3] - it[2]
            mix_degree = float(min(read_count, write_count)) / (read_count + write_count)
            bw = (read_count + write_count) * 64 / total_time # B/ns
            bw = bw * 1e9 / (1024 * 1024) # MB/s
            print(f"{mix_degree},{bw}")
            agg_read += read_count
            agg_write += write_count
        agg_time = agg_end - agg_start
        overall_mix = float(min(agg_read, agg_write)) / (agg_read + agg_write)
        overall_bw = (agg_read + agg_write) * 64 / agg_time # B/ns
        overall_bw = overall_bw * 1e9 / (1024 * 1024) # MB/s
        print("Overall:")
        print(f"{overall_mix},{overall_bw}")
    elif args.report == "avg_lat":
        print(f"{final_avg_latency}")
    elif args.report == "avg_wait_inv":
        print(f"{avg_wait_inv}")

# print("Average Latency: ", final_avg_latency, "ns")
# print("Throughput: ", final_bw, "MB/s")

if args.compare_baseline:
    def parse_summary_metrics(path):
        metrics = {}
        with open(path, 'r') as f:
            in_summary = False
            for line in f:
                line = line.strip()
                if not line:
                    continue
                if line == "metric,value":
                    in_summary = True
                    continue
                if in_summary:
                    parts = line.split(',', 1)
                    if len(parts) == 2:
                        try:
                            metrics[parts[0]] = float(parts[1])
                        except ValueError:
                            in_summary = False
                    else:
                        in_summary = False
                    continue
                if line.startswith("id,type"):
                    break
        return metrics

    current_metrics = parse_summary_metrics(args.file)
    baseline_metrics = parse_summary_metrics(args.compare_baseline)

    if not current_metrics:
        print("No summary metrics found in current CSV")
    elif not baseline_metrics:
        print("No summary metrics found in baseline CSV")
    else:
        key_metrics = [
            ("bw_gbps", "Aggregate Bandwidth (GB/s)"),
            ("avg_latency_ns", "Average Latency (ns)"),
            ("avg_evict_wait_ns", "Avg Evict Wait (ns)"),
            ("efficiency", "Bus Efficiency"),
            ("avg_utilization", "Bus Utilization"),
        ]
        print()
        print("| Metric | Baseline | Current | Delta % |")
        print("|---|---|---|---|")
        for suffix, label in key_metrics:
            base_val = None
            cur_val = None
            for k, v in current_metrics.items():
                if k.endswith(":" + suffix):
                    cur_val = v
                    break
            for k, v in baseline_metrics.items():
                if k.endswith(":" + suffix):
                    base_val = v
                    break
            if base_val is not None and cur_val is not None:
                delta = ((cur_val - base_val) / base_val * 100) if base_val != 0 else 0
                print(f"| {label} | {base_val:.4f} | {cur_val:.4f} | {delta:+.2f}% |")
            elif base_val is None:
                print(f"| {label} | N/A | {cur_val:.4f} | N/A |")
            elif cur_val is None:
                print(f"| {label} | {base_val:.4f} | N/A | N/A |")
