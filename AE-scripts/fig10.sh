#!/bin/bash

scales=("4" "8" "16")
topos=("chain" "tree" "ring" "spineleaf" "full")

# If arg is 'r', only run report and gather
if [[ "$1" == "r" ]]; then
    mkdir -p output/fig10
    mkdir -p output/fig10/summary
    for scale in ${scales[@]}; do
    for topo in ${topos[@]}
    do
        python3 output/report.py output/fig10/${topo}.csv --report=bw > output/fig10/${topo}${scale}_bw.txt
        cp output/fig10/${topo}${scale}_bw.txt output/fig10/summary/${topo}${scale}_bw.txt
    done
    done
    exit 0
fi

# Build config files
mkdir -p configs/fig10
for scale in ${scales[@]}; do
for topo in ${topos[@]}
do
    python3 configs/topos.py --max_clock=4000000 --bw=4096 --epnum=${scale} --topo=${topo} \
        --bus --cfgname="configs/fig10/${topo}${scale}.toml" --outputdir="fig10"
done
done
 
# Run Xerxes
mkdir -p output/fig10
mkdir -p output/fig10/summary
for scale in ${scales[@]}; do 
for topo in ${topos[@]}
do
    build/Xerxes configs/fig10/${topo}${scale}.toml 1> output/fig10/${topo}.out 2> output/fig10/${topo}.err &
done
wait

# Report
for topo in ${topos[@]}
do
    python3 output/report.py output/fig10/${topo}.csv --report=bw > output/fig10/${topo}${scale}_bw.txt
done

# Gather reports
for topo in ${topos[@]}
do
    cp output/fig10/${topo}${scale}_bw.txt output/fig10/summary/${topo}${scale}_bw.txt
done
done