#!/usr/bin/env python3
"""
Generate Figure 15-16: Bandwidth and Bus Utility/Transmission Efficiency plots
"""
import os
import re
import sys

try:
    import numpy as np
    import matplotlib
    matplotlib.use("Agg")  # Use non-interactive backend
    import matplotlib.pyplot as plt
except ImportError:
    print("Error: Please install required packages: numpy, matplotlib")
    print("Run: pip install numpy matplotlib")
    sys.exit(1)

from pathlib import Path

SUMMARY_DIR = Path(__file__).parent / "fig1516" / "summary"
OUTPUT_DIR = Path(__file__).parent / "fig1516"

FSIZES = [0, 1, 8, 16, 32, 64]
RATIOS = ["0.0", "0.25", "0.33", "0.5", "0.66", "0.75", "1.0"]

# mapping of ratio to R:W ratio
RATIO_TO_RW = {
    "0.0": "1:0",
    "0.25": "3:1",
    "0.33": "2:1",
    "0.5": "1:1",
    "0.66": "1:2",
    "0.75": "1:3",
    "1.0": "0:1"
}

# mapping of color to R:W ratio
COLORS = {
    "1:0": "#FFFFFF",  # white
    "3:1": "#D0E3FF",
    "2:1": "#A3C7FF",
    "1:1": "#6699FF",  # medium blue
    "1:2": "#4D7ACC",
    "1:3": "#335C99",
    "0:1": "#1A3E66"  # dark blue
}


def parse_result_file(filepath):
    try:
        with open(filepath, 'r') as f:
            content = f.read()
    except FileNotFoundError:
        return None, None, None
    
    bw_match = re.search(r'\* Aggregate:.*?\n\s+- Bandwidth \(GB/s\): ([\d.]+)', content, re.DOTALL)
    bandwidth = float(bw_match.group(1)) if bw_match else None
    
    util_match = re.search(r'Average utilization: ([\d.]+)', content)
    bus_utility = float(util_match.group(1)) if util_match else None
    
    eff_match = re.search(r'Efficiency: ([\d.]+)', content)
    efficiency = float(eff_match.group(1)) if eff_match else None
    
    return bandwidth, bus_utility, efficiency


def load_all_data():
    data = {}
    
    for fsize in FSIZES:
        data[fsize] = {}
        for ratio in RATIOS:
            filepath = SUMMARY_DIR / str(fsize) / f"{ratio}.txt"
            if filepath.exists():
                bandwidth, bus_utility, efficiency = parse_result_file(filepath)
                data[fsize][ratio] = {
                    'bandwidth': bandwidth,
                    'bus_utility': bus_utility,
                    'efficiency': efficiency
                }
            else:
                print(f"Warning: {filepath} not found")
    
    return data


def normalize_bandwidth(data):
    normalized = {}
    
    for fsize in FSIZES:
        if fsize not in data:
            continue
        
        baseline = data[fsize].get("0.0", {}).get('bandwidth')
        if baseline is None or baseline == 0:
            continue
        
        normalized[fsize] = {}
        for ratio in RATIOS:
            if ratio in data[fsize]:
                bw = data[fsize][ratio].get('bandwidth')
                if bw is not None:
                    normalized[fsize][ratio] = bw / baseline
    
    return normalized


def calculate_avg_efficiency(data):
    avg_efficiency = {}
    
    for fsize in FSIZES:
        if fsize not in data:
            continue
        
        efficiencies = []
        for ratio in RATIOS:
            if ratio in data[fsize]:
                eff = data[fsize][ratio].get('efficiency')
                if eff is not None:
                    efficiencies.append(eff)
        
        if efficiencies:
            avg_efficiency[fsize] = np.mean(efficiencies)
    
    return avg_efficiency


def get_x_labels():
    labels = []
    positions = []
    
    labels.append("0")
    positions.append(0)
    
    labels.append("0")
    positions.append(1)
    
    for fsize in [8, 16, 32, 64]:
        if fsize == 8:
            labels.append("1/8")
        elif fsize == 16:
            labels.append("1/4")
        elif fsize == 32:
            labels.append("1/2")
        elif fsize == 64:
            labels.append("1")
        positions.append(fsize / 64.0)
    
    x_positions = list(range(len(labels)))
    
    return labels, x_positions


def plot_figures(data):
    norm_bw = normalize_bandwidth(data)
    
    avg_eff = calculate_avg_efficiency(data)
    
    x_labels, x_positions = get_x_labels()
    
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 6))
    
    fsize_to_xpos = {
        0: 0,   # Half-duplex first 0
        1: 1,   # Half-duplex second 0 (actually full-duplex's 0)
        8: 2,   # 1/8
        16: 3,  # 1/4
        32: 4,  # 1/2    
        64: 5   # 1
    }
    
    bar_width = 0.12
    x_base = np.array(x_positions)
    
    # plot left figure: Normalized Bandwidth
    ratio_order = ["0.0", "0.25", "0.33", "0.5", "0.66", "0.75", "1.0"]
    for idx, ratio in enumerate(ratio_order):
        rw_label = RATIO_TO_RW[ratio]
        color = COLORS[rw_label]
        values = []
        x_vals = []
        
        for fsize in FSIZES:
            xpos = fsize_to_xpos.get(fsize)
            if xpos is not None and fsize in norm_bw and ratio in norm_bw[fsize]:
                values.append(norm_bw[fsize][ratio])
                x_vals.append(x_base[xpos] + (idx - 3) * bar_width)
        
        if values:
            ax1.bar(x_vals, values, bar_width, label=rw_label, color=color, edgecolor='black', linewidth=0.5)
    
    ax1.set_xlabel('Normalized header overhead', fontsize=12)
    ax1.set_ylabel('Norm. bandwidth', fontsize=12)
    ax1.set_xticks(x_base)
    ax1.set_xticklabels(x_labels)
    ax1.set_ylim(0, 2.2)
    ax1.grid(True, alpha=0.3, axis='y')
    ax1.legend(title='R:W ratio', ncol=7, loc='upper right', fontsize=8)
    
    # add Half-duplex and Full-duplex labels
    divider_x = (x_base[0] + x_base[1]) / 2.0
    ax1.axvline(x=divider_x, color='blue', linestyle='--', linewidth=1, alpha=0.5)
    ax1.text((x_base[0] + divider_x) / 2, ax1.get_ylim()[1] * 0.95, 'Half-duplex', ha='center', fontsize=10, weight='bold')
    ax1.text((x_base[2] + x_base[-1]) / 2, ax1.get_ylim()[1] * 0.95, 'Full-duplex', ha='center', fontsize=10, weight='bold')
    
    # plot right figure: Bus Utility and Transmission Efficiency
    for idx, ratio in enumerate(ratio_order):
        rw_label = RATIO_TO_RW[ratio]
        color = COLORS[rw_label]
        values = []
        x_vals = []
        
        for fsize in FSIZES:
            xpos = fsize_to_xpos.get(fsize)
            if xpos is not None and fsize in data and ratio in data[fsize]:
                bus_util = data[fsize][ratio].get('bus_utility')
                if bus_util is not None:
                    values.append(bus_util)
                    x_vals.append(x_base[xpos] + (idx - 3) * bar_width)
        
        if values:
            ax2.bar(x_vals, values, bar_width, label=rw_label, color=color, edgecolor='black', linewidth=0.5)
    
    eff_x = []
    eff_y = []
    for fsize in FSIZES:
        xpos = fsize_to_xpos.get(fsize)
        if xpos is not None and fsize in avg_eff:
            eff_x.append(x_base[xpos])
            eff_y.append(avg_eff[fsize])
    
    ax2_twin = ax2.twinx()
    ax2_twin.plot(eff_x, eff_y, 'r^-', linewidth=2, markersize=8, label='Trans. efficiency')
    ax2_twin.set_ylabel('Trans. efficiency', fontsize=12, color='red')
    ax2_twin.tick_params(axis='y', labelcolor='red')
    ax2_twin.set_ylim(0, 1.5)
    
    ax2.set_xlabel('Normalized header overhead', fontsize=12)
    ax2.set_ylabel('Bus utility', fontsize=12)
    ax2.set_xticks(x_base)
    ax2.set_xticklabels(x_labels)
    ax2.set_ylim(0, 1.5)
    ax2.grid(True, alpha=0.3, axis='y')
    ax2.legend(title='R:W ratio', ncol=7, loc='upper right', fontsize=8)
    
    divider_x = (x_base[0] + x_base[1]) / 2.0
    ax2.axvline(x=divider_x, color='blue', linestyle='--', linewidth=1, alpha=0.5)
    ax2.text((x_base[0] + divider_x) / 2, ax2.get_ylim()[1] * 0.95, 'Half-duplex', ha='center', fontsize=10, weight='bold')
    ax2.text((x_base[2] + x_base[-1]) / 2, ax2.get_ylim()[1] * 0.95, 'Full-duplex', ha='center', fontsize=10, weight='bold')
    
    plt.tight_layout()
    
    output_path = OUTPUT_DIR / "fig1516.png"
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    print(f"Figure saved to {output_path}")
    
    plt.close()


def main():
    print("Loading data...")
    data = load_all_data()
    
    print("Plotting figures...")
    plot_figures(data)
    
    print("Done!")


if __name__ == "__main__":
    main()

