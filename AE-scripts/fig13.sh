#!/bin/bash

policies=("FIFO" "LRU" "LFI" "LIFO" "MRU")

# Build config files
mkdir -p configs/fig13
for policy in ${policies[@]}
do
    python3 configs/victim.py --max_clock=4000000 --policy=${policy} \
        --cfgname="configs/fig13/${policy}.toml" --outputdir="fig13_rev" --workload=fig13
done
 
# Run Xerxes
mkdir -p output/fig13_rev
for policy in ${policies[@]}
do
    build/Xerxes configs/fig13/${policy}.toml 1> output/fig13_rev/${policy}.out 2> output/fig13_rev/${policy}.err &
done

wait

# Gather outputs and plot (no report needed; metrics from .err)
mkdir -p output/fig13_rev/summary
for policy in ${policies[@]}
do
    cp output/fig13_rev/${policy}.err output/fig13_rev/summary/${policy}.err
done