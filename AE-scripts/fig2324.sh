#!/bin/bash

fsizes=("0" "1" "8" "16" "32" "64") # 0 for half-duplex notation
ratios=("0.0" "0.25" "0.33" "0.5" "0.66" "0.75" "1.0")

# If arg is 'r', only run report and gather
if [[ "$1" == "r" ]]; then
    mkdir -p output/fig2324
    mkdir -p output/fig2324/summary
    for fsize in ${fsizes[@]}; do
    # {
    mkdir -p output/fig2324/summary/${fsize}
    for ratio in ${ratios[@]}
    do
        cp output/fig2324/${ratio}.csv output/fig2324/summary/${fsize}/${ratio}.txt
    done
    # }
    done
    exit 0
fi

# Build config files
mkdir -p configs/fig2324
mkdir -p output/fig2324
mkdir -p output/fig2324/summary

for fsize in ${fsizes[@]}; do
# {
for ratio in ${ratios[@]}
do
    python3 configs/bus.py --max_clock=4000000 --ratio=${ratio} --fsize=${fsize} \
        --cfgname="configs/fig2324/${ratio}.toml" --outputdir="fig2324"
done
 
# Run Xerxes
for ratio in ${ratios[@]}
do
    build/Xerxes configs/fig2324/${ratio}.toml 1> output/fig2324/${ratio}.out 2> output/fig2324/${ratio}.err &
done

wait

# Report
# for ratio in ${ratios[@]}
# do
#     python3 output/report.py output/fig2324/${ratio}.csv --report=bw > output/fig2324/${ratio}_bw.csv
# done

# Gather reports
mkdir -p output/fig2324/summary/${fsize}
for ratio in ${ratios[@]}
do
    cp output/fig2324/${ratio}.csv output/fig2324/summary/${fsize}/${ratio}.txt
done
# }
done
