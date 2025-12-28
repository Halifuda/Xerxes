#!/bin/bash

topos=("chain" "tree" "ring" "spineleaf" "full")

# If arg is 'r', only run report and gather
if [[ "$1" == "r" ]]; then
    mkdir -p output/fig11
    # Report
    for topo in ${topos[@]}
    do
        python3 output/report.py output/fig11/${topo}.csv --report=hoplat > output/fig11/${topo}_hoplat.csv
    done

    # Gather reports
    mkdir -p output/fig11/summary
    for topo in ${topos[@]}
    do
        cp output/fig11/${topo}_hoplat.csv output/fig11/summary/${topo}_hoplat.csv
    done

    exit 0
fi

# Build config files
mkdir -p configs/fig11
for topo in ${topos[@]}
do
    python3 configs/topos.py --max_clock=4000000 --bw=4096 --epnum=16 --topo=${topo} \
        --norm --cfgname="configs/fig11/${topo}.toml" --outputdir="fig11"
done
 
# Run Xerxes
mkdir -p output/fig11
for topo in ${topos[@]}
do
    build/Xerxes configs/fig11/${topo}.toml 1> output/fig11/${topo}.out 2> output/fig11/${topo}.err &
done

wait

# Report
for topo in ${topos[@]}
do
    python3 output/report.py output/fig11/${topo}.csv --report=hoplat > output/fig11/${topo}_hoplat.csv
done

# Gather reports
mkdir -p output/fig11/summary
for topo in ${topos[@]}
do
    cp output/fig11/${topo}_hoplat.csv output/fig11/summary/${topo}_hoplat.csv
done
