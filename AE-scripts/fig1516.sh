#!/bin/bash

fsizes=("0" "1" "8" "16" "32" "64") # 0 for half-duplex notation
ratios=("0.0" "0.25" "0.33" "0.5" "0.66" "0.75" "1.0")

# If arg is 'r', only run report and gather
if [[ "$1" == "r" ]]; then
    mkdir -p output/fig1516
    mkdir -p output/fig1516/summary
    for fsize in ${fsizes[@]}; do
    # {
    mkdir -p output/fig1516/summary/${fsize}
    for ratio in ${ratios[@]}
    do
        cp output/fig1516/${ratio}.err output/fig1516/summary/${fsize}/${ratio}.txt
    done
    # }
    done
    exit 0
fi

# Build config files
mkdir -p configs/fig1516
mkdir -p output/fig1516
mkdir -p output/fig1516/summary

for fsize in ${fsizes[@]}; do
# {
for ratio in ${ratios[@]}
do
    python3 configs/bus.py --max_clock=4000000 --ratio=${ratio} --fsize=${fsize} \
        --cfgname="configs/fig1516/${ratio}.toml" --outputdir="fig1516"
done
 
# Run Xerxes
for ratio in ${ratios[@]}
do
    build/Xerxes configs/fig1516/${ratio}.toml 1> output/fig1516/${ratio}.out 2> output/fig1516/${ratio}.err &
done

wait

# Report
# for ratio in ${ratios[@]}
# do
#     python3 output/report.py output/fig1516/${ratio}.csv --report=bw > output/fig1516/${ratio}_bw.csv
# done

# Gather reports
mkdir -p output/fig1516/summary/${fsize}
for ratio in ${ratios[@]}
do
    cp output/fig1516/${ratio}.err output/fig1516/summary/${fsize}/${ratio}.txt
done
# }
done
