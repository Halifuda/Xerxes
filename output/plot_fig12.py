#!/usr/bin/env python3
"""
Generate Figure 12: Normalized latency and throughput comparison across topologies
"""

import os
import sys
from pathlib import Path

try:
    import numpy as np
    import matplotlib
    matplotlib.use("Agg")  # Use non-interactive backend
    import matplotlib.pyplot as plt
except ImportError:
    print("Error: Please install required packages: numpy, matplotlib")
    print("Run: pip install numpy matplotlib")
    sys.exit(1)

# configuration
SCRIPT_DIR = Path(__file__).parent
SUMMARY_DIR = SCRIPT_DIR / "fig12" / "summary"
OUTPUT_DIR = SCRIPT_DIR / "fig12"

# mapping of topologies and their labels
TOPOLOGIES = [
    ('chain', 'Chain'),
    ('tree', 'Tree'),
    ('ring', 'Ring'),
    ('spineleaf', 'Spine-leaf'),
    ('full', 'Fully-connected')
]

# benchmark list
BENCHMARKS = [
    'BTree-mini',
    'liblinear-mini',
    'redis-mini',
    'silo-mini',
    'XSBench-mini'
]

# color mapping
COLORS = {
    'chain': '#FFFFFF',      # white
    'tree': '#808080',       # gray
    'ring': '#FFD700',       # yellow
    'spineleaf': '#00CED1',  # cyan/blue-green
    'full': '#4169E1'        # blue
}

# edge color (for white bar chart of chain)
EDGE_COLORS = {
    'chain': 'black',
    'tree': 'black',
    'ring': 'black',
    'spineleaf': 'black',
    'full': 'black'
}


def read_csv_value(filepath):
    try:
        with open(filepath, 'r') as f:
            line = f.readline().strip()
            if line:
                return float(line)
    except (FileNotFoundError, ValueError, IOError):
        return None
    return None


def load_data():
    avg_lat_data = {}
    bw_data = {}
    
    for topo_name, topo_label in TOPOLOGIES:
        avg_lat_data[topo_name] = {}
        bw_data[topo_name] = {}
        
        for benchmark in BENCHMARKS:
            # read average latency data
            avg_lat_file = SUMMARY_DIR / "avg_lat" / f"{topo_name}_{benchmark}_avg_lat.csv"
            avg_lat_value = read_csv_value(avg_lat_file)
            if avg_lat_value is not None:
                avg_lat_data[topo_name][benchmark] = avg_lat_value
            
            # read bandwidth data
            bw_file = SUMMARY_DIR / "bw" / f"{topo_name}_{benchmark}_bw.csv"
            bw_value = read_csv_value(bw_file)
            if bw_value is not None:
                bw_data[topo_name][benchmark] = bw_value
    
    return avg_lat_data, bw_data


def normalize_data(data, baseline_topos=['chain']):
    normalized = {}
    
    for topo_name, topo_label in TOPOLOGIES:
        normalized[topo_name] = {}
        for benchmark in BENCHMARKS:
            baseline_values = []
            for baseline_topo in baseline_topos:
                if benchmark in data[baseline_topo]:
                    baseline_values.append(data[baseline_topo][benchmark])
            
            if baseline_values:
                baseline_value = max(baseline_values)
            else:
                baseline_value = None
            
            if baseline_value is not None and baseline_value != 0:
                if benchmark in data[topo_name]:
                    normalized[topo_name][benchmark] = data[topo_name][benchmark] / baseline_value
                else:
                    normalized[topo_name][benchmark] = None
            else:
                normalized[topo_name][benchmark] = None
    
    return normalized


def plot_figures(avg_lat_data, bw_data):
    """Plot two figures"""
    norm_lat = normalize_data(avg_lat_data, baseline_topos=['chain'])
    norm_bw = normalize_data(bw_data, baseline_topos=['chain'])
    
    # create figures
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 6))
    
    x = np.arange(len(BENCHMARKS))
    bar_width = 0.15
    
    # plot the left figure: Normalized Latency
    for idx, (topo_name, topo_label) in enumerate(TOPOLOGIES):
        values = []
        for benchmark in BENCHMARKS:
            if benchmark in norm_lat[topo_name]:
                values.append(norm_lat[topo_name][benchmark])
            else:
                values.append(0)
        
        x_pos = x + (idx - 2) * bar_width
        color = COLORS[topo_name]
        edgecolor = EDGE_COLORS[topo_name]
        linewidth = 1.5 if topo_name == 'chain' else 0.5
        
        ax1.bar(x_pos, values, bar_width, label=topo_label, 
                color=color, edgecolor=edgecolor, linewidth=linewidth)
    
    ax1.set_xlabel('Benchmark', fontsize=12)
    ax1.set_ylabel('Norm. latency', fontsize=12)
    ax1.set_xticks(x)
    ax1.set_xticklabels(['Btree', 'liblinear', 'redis', 'silo', 'XSBench'], fontsize=10)
    ax1.set_ylim(0, 1.2)
    ax1.grid(True, alpha=0.3, axis='y')
    ax1.legend(loc='upper right', fontsize=9, frameon=True)
    ax1.set_axisbelow(True)
    
    # plot the right figure: Normalized Throughput
    for idx, (topo_name, topo_label) in enumerate(TOPOLOGIES):
        values = []
        for benchmark in BENCHMARKS:
            if benchmark in norm_bw[topo_name]:
                values.append(norm_bw[topo_name][benchmark])
            else:
                values.append(0)
        
        x_pos = x + (idx - 2) * bar_width
        color = COLORS[topo_name]
        edgecolor = EDGE_COLORS[topo_name]
        linewidth = 1.5 if topo_name == 'chain' else 0.5
        
        ax2.bar(x_pos, values, bar_width, label=topo_label,
                color=color, edgecolor=edgecolor, linewidth=linewidth)
    
    ax2.set_xlabel('Benchmark', fontsize=12)
    ax2.set_ylabel('Norm. throughput', fontsize=12)
    ax2.set_xticks(x)
    ax2.set_xticklabels(['Btree', 'liblinear', 'redis', 'silo', 'XSBench'], fontsize=10)
    ax2.set_ylim(0, 4)
    ax2.grid(True, alpha=0.3, axis='y')
    ax2.legend(loc='upper right', fontsize=9, frameon=True)
    ax2.set_axisbelow(True)
    
    plt.tight_layout()
    
    # save figures
    output_path = OUTPUT_DIR / "fig12.png"
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    print(f"Figure saved to {output_path}")
    
    pdf_path = OUTPUT_DIR / "fig12.pdf"
    plt.savefig(pdf_path, bbox_inches='tight')
    print(f"Figure saved to {pdf_path}")
    
    plt.close()


def main():
    print("Loading data...")
    avg_lat_data, bw_data = load_data()
    
    print("Plotting figures...")
    plot_figures(avg_lat_data, bw_data)
    
    print("Done!")


if __name__ == "__main__":
    main()

