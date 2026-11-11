import matplotlib.pyplot as plt
import numpy as np
import os

def read_bw(file_path):
    with open(file_path, 'r') as f:
        return float(f.read().strip())

def main():
    output_dir = 'output'
    fig_data_dir = os.path.join(output_dir, 'fig10')
    
    # Topologies and scales from the plot
    topos = ['chain', 'tree', 'ring', 'spineleaf', 'full']
    topo_labels = ['Chain', 'Tree', 'Ring', 'SL', 'FC']
    scales = [4, 8, 16]
    
    # Read normalization value
    norm_bw_file = os.path.join(fig_data_dir, 'chain4_bw.txt')
    norm_bw = read_bw(norm_bw_file)
    
    # Read data
    data = {}
    for topo in topos:
        data[topo] = []
        for scale in scales:
            bw_file = os.path.join(fig_data_dir, f'{topo}{scale}_bw.txt')
            if os.path.exists(bw_file):
                bw = read_bw(bw_file)
                normalized_bw = bw / norm_bw
                data[topo].append(normalized_bw)
            else:
                data[topo].append(0) # Append 0 if file doesn't exist

    # Plotting
    x = np.arange(len(topo_labels))
    width = 0.25
    
    fig, ax = plt.subplots(figsize=(8, 5))
    
    # Bar colors from the image
    colors = ['#ffffff', '#48b0a6', '#0077cc']
    edge_color = 'black'

    for i, scale in enumerate(scales):
        bar_positions = x - width + i * width
        heights = [data[topo][i] for topo in topos]
        ax.bar(bar_positions, heights, width, label=str(scale), color=colors[i], edgecolor=edge_color)

    # Add some text for labels, title and axes ticks
    ax.set_ylabel('Norm. bandwidth', fontsize=14)
    ax.set_title('System scale =', loc='right', fontsize=14)
    ax.set_xticks(x)
    ax.set_xticklabels(topo_labels, fontsize=14)
    ax.tick_params(axis='y', labelsize=14)
    
    ax.set_ylim(0, 8)
    ax.yaxis.grid(True, linestyle='dotted')
    
    # Legend
    ax.legend(ncol=3, loc='upper right', bbox_to_anchor=(0.9, 1.1), fontsize=12, handlelength=1.5, frameon=True, edgecolor='black')

    plt.tight_layout()
    
    # Save the figure
    fig_dir = os.path.join(output_dir, 'fig10')
    plt.savefig(os.path.join(fig_dir, 'fig10.png'))
    print(f"Plot saved to {os.path.join(fig_dir, 'fig10.png')}")

if __name__ == '__main__':
    main()
