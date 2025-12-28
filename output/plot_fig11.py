#!/usr/bin/env python3
"""
Plot the hop latency figure for different topologies
Based on the CSV files in the summary folder
"""

import os
import sys
import csv
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt



plt.rcParams['font.sans-serif'] = ['Arial', 'DejaVu Sans', 'Liberation Sans']
plt.rcParams['axes.unicode_minus'] = False

def load_data(csv_path):
    """Load the CSV data file"""
    hops = []
    latencies = []
    with open(csv_path, 'r') as f:
        reader = csv.reader(f)
        for row in reader:
            if len(row) >= 2 and row[0].strip() and row[1].strip():
                try:
                    hop = int(float(row[0].strip()))
                    latency = float(row[1].strip())
                    hops.append(hop)
                    latencies.append(latency)
                except ValueError:
                    continue
    return {'hop': hops, 'latency': latencies}

def plot_hoplat_figure(summary_dir, output_path):
    """Plot the hop latency figure"""
    
    # Define the topology list and the corresponding file names
    topologies = [
        ('chain', '(a) Chain'),
        ('tree', '(b) Tree'),
        ('ring', '(c) Ring'),
        ('spineleaf', '(d) Spine-leaf'),
        ('full', '(e) Fully-connected')
    ]
    
    # Create a figure, 5 subplots horizontally arranged
    fig, axes = plt.subplots(1, 5, figsize=(16, 3.5))
    fig.tight_layout(pad=2.0)
    
    for idx, (topo_name, title) in enumerate(topologies):
        ax = axes[idx]
        csv_file = os.path.join(summary_dir, f'{topo_name}_hoplat.csv')
        
        if not os.path.exists(csv_file):
            print(f"Warning: {csv_file} not found, skipping...")
            ax.text(0.5, 0.5, 'No data', ha='center', va='center', transform=ax.transAxes)
            ax.set_title(title)
            continue
        
        # Load the data
        data = load_data(csv_file)
        hops = data['hop']
        latencies = data['latency']
        
        if len(hops) == 0:
            ax.text(0.5, 0.5, 'No data', ha='center', va='center', transform=ax.transAxes)
            ax.set_title(title)
            continue
        
        # Plot the bar chart
        bars = ax.bar(hops, latencies, width=0.6, color='steelblue', edgecolor='black', linewidth=0.5)
        
        # Set the title
        ax.set_title(title, fontsize=11, pad=8)
        
        # Set the X axis
        ax.set_xlabel('#Hop', fontsize=10)
        max_hop = max(hops)
        min_hop = min(hops)
        ax.set_xlim(min_hop - 0.5, max_hop + 0.5)
        
        # Set the X axis ticks - show all actual hop values
        unique_hops = sorted(set(hops))
        ax.set_xticks(unique_hops)
        
        # Set the Y axis - show y-axis labels for all subplots
        ax.set_ylabel('Average latency (ns)', fontsize=10)
        
        # Set the Y axis ticks, adjust according to the data range
        max_latency = max(latencies)
        if max_latency <= 800:
            y_max = 800
            y_ticks = [0, 200, 400, 600, 800]
        else:
            y_max = 1200
            y_ticks = [0, 400, 800, 1200]
        
        ax.set_ylim(0, y_max)
        ax.set_yticks(y_ticks)
        
        # Grid lines
        ax.grid(True, alpha=0.3, linestyle='--', axis='y')
        ax.set_axisbelow(True)
    
    # Adjust the subplot spacing - need more left margin for y-axis labels
    plt.subplots_adjust(left=0.12, right=0.98, top=0.90, bottom=0.15, wspace=0.30)
    
    # Save the image
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    print(f"Figure saved to: {output_path}")
    
    pdf_path = output_path.replace('.png', '.pdf')
    plt.savefig(pdf_path, bbox_inches='tight')
    print(f"Figure saved to: {pdf_path}")
    
    plt.close()

if __name__ == '__main__':
    script_dir = os.path.dirname(os.path.abspath(__file__))
    summary_dir = os.path.join(script_dir, 'fig11/summary')
    output_path = os.path.join(script_dir, 'fig11/hoplat.png')
    
    if not os.path.exists(summary_dir):
        print(f"Error: Summary directory not found: {summary_dir}")
        sys.exit(1)
    
    plot_hoplat_figure(summary_dir, output_path)

