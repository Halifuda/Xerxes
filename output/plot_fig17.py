#!/usr/bin/env python3
"""
Generate Figure 17: Full-duplex bus performance comparison
(a) Execution speedup and mix degrees across benchmarks
(b) Normalized bandwidth vs mix degree for silo
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

# Configuration
SCRIPT_DIR = Path(__file__).parent
SUMMARY_DIR = SCRIPT_DIR / "fig17" / "summary"
FULLBUS_DIR = SUMMARY_DIR / "fullbus"
HALFBUS_DIR = SUMMARY_DIR / "halfbus"
OUTPUT_DIR = SCRIPT_DIR / "fig17"

# Benchmark mapping (order as shown in the figure)
BENCHMARKS = [
    'liblinear',
    'XSBench',
    'BTree',
    'redis',
    'silo'
]


def read_halfbus_bw(benchmark):
    filepath = HALFBUS_DIR / f"{benchmark}_bw.csv"
    try:
        with open(filepath, 'r') as f:
            line = f.readline().strip()
            if line:
                return float(line)
    except (FileNotFoundError, ValueError, IOError) as e:
        print(f"Warning: Could not read {filepath}: {e}")
        return None
    return None


def read_fullbus_rw(benchmark):
    filepath = FULLBUS_DIR / f"{benchmark}_rw.csv"
    try:
        with open(filepath, 'r') as f:
            lines = f.readlines()
            
        # Find the "Overall:" line
        overall_idx = None
        for i, line in enumerate(lines):
            if line.strip().startswith("Overall:"):
                overall_idx = i
                break
        
        if overall_idx is None:
            print(f"Warning: Could not find 'Overall:' in {filepath}")
            return None, None, None
        
        # Read overall data (line after "Overall:")
        if overall_idx + 1 < len(lines):
            overall_line = lines[overall_idx + 1].strip()
            parts = overall_line.split(',')
            if len(parts) >= 2:
                overall_mix_degree = float(parts[0])
                overall_bw = float(parts[1])
            else:
                print(f"Warning: Invalid overall data format in {filepath}")
                return None, None, None
        else:
            print(f"Warning: No data after 'Overall:' in {filepath}")
            return None, None, None
        
        # Read all data points before "Overall:"
        data_points = []
        for i in range(overall_idx):
            line = lines[i].strip()
            if line and not line.startswith("Overall:"):
                parts = line.split(',')
                if len(parts) >= 2:
                    try:
                        mix_degree = float(parts[0])
                        bw = float(parts[1])
                        data_points.append((mix_degree, bw))
                    except ValueError:
                        continue
        
        return overall_mix_degree, overall_bw, data_points
        
    except (FileNotFoundError, ValueError, IOError) as e:
        print(f"Warning: Could not read {filepath}: {e}")
        return None, None, None


def plot_fig17():
    # Load data for left plot (a)
    speedups = []
    mix_degrees = []
    
    for benchmark in BENCHMARKS:
        halfbus_bw = read_halfbus_bw(benchmark)
        overall_mix_degree, overall_bw, _ = read_fullbus_rw(benchmark)
        
        if halfbus_bw is not None and overall_bw is not None and halfbus_bw != 0:
            # Calculate speedup percentage: (fullbus_bw / halfbus_bw - 1) * 100
            speedup = (overall_bw / halfbus_bw - 1) * 100
            speedups.append(speedup)
        else:
            speedups.append(0)
        
        if overall_mix_degree is not None:
            mix_degrees.append(overall_mix_degree)
        else:
            mix_degrees.append(0)
    
    # Load data for right plot (b) - only silo
    silo_mix_degrees = []
    silo_bws = []
    
    _, _, silo_data_points = read_fullbus_rw('silo')
    if silo_data_points:
        for mix_degree, bw in silo_data_points:
            silo_mix_degrees.append(mix_degree)
            silo_bws.append(bw)
    
    # Create figure with two subplots
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 6))
    
    # Plot (a): Execution Speedup and Mix Degrees
    x = np.arange(len(BENCHMARKS))
    
    # Bar chart for speedup
    bars = ax1.bar(x, speedups, color='lightblue', alpha=0.7, width=0.6)
    ax1.set_xlabel('Benchmark', fontsize=12)
    ax1.set_ylabel('Speed up (%)', fontsize=12, color='blue')
    ax1.set_xticks(x)
    ax1.set_xticklabels(BENCHMARKS, fontsize=10)
    ax1.set_ylim(0, 12)
    ax1.grid(True, alpha=0.3, axis='y')
    ax1.tick_params(axis='y', labelcolor='blue')
    
    # Line chart for mix degree (right y-axis)
    ax1_twin = ax1.twinx()
    line = ax1_twin.plot(x, mix_degrees, 'ro-', linewidth=2, markersize=8, label='Mix degree')
    ax1_twin.set_ylabel('Mix degree', fontsize=12, color='red')
    ax1_twin.set_ylim(0, 0.20)
    ax1_twin.tick_params(axis='y', labelcolor='red')
    
    # Plot (b): Normalized Bandwidth vs Mix Degree for Silo
    if silo_mix_degrees and silo_bws:
        ax2.scatter(silo_mix_degrees, silo_bws, s=30, color='blue', alpha=0.6, marker='s')
        ax2.set_xlabel('Mix degree', fontsize=12)
        ax2.set_ylabel('Bandwidth', fontsize=12)
        ax2.set_xlim(0, 0.30)
        ax2.grid(True, alpha=0.3)
    
    plt.tight_layout()
    
    # Save figures
    output_path = OUTPUT_DIR / "fig17.png"
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    print(f"Figure saved to {output_path}")
    
    pdf_path = OUTPUT_DIR / "fig17.pdf"
    plt.savefig(pdf_path, bbox_inches='tight')
    print(f"Figure saved to {pdf_path}")
    
    plt.close()


def main():
    print("Loading data...")
    print("Plotting Figure 17...")
    plot_fig17()
    print("Done!")


if __name__ == "__main__":
    main()

