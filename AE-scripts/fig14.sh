#!/bin/bash

# Burst invalidation lengths to test
lens=("0" "1" "2" "3")
policy="LIFO"

# If arg is 'r', only regenerate reports and gather
if [[ "$1" == "r" ]]; then
    mkdir -p output/fig14/summary
    for len in ${lens[@]}; do
        python3 output/report.py output/fig14/len-${len}.csv --report=avg_lat > output/fig14/len-${len}_avglat.csv
        python3 output/report.py output/fig14/len-${len}.csv --report=avg_wait_inv > output/fig14/len-${len}_avgwaitinv.csv
        cp output/fig14/len-${len}_avglat.csv output/fig14/summary/len-${len}_avglat.csv
        cp output/fig14/len-${len}_avgwaitinv.csv output/fig14/summary/len-${len}_avgwaitinv.csv
        cp output/fig14/len-${len}.err output/fig14/summary/len-${len}.err
    done
    # Plot using existing logs
    python3 scripts/plot_fig14.py output/fig14
    exit 0
fi

mkdir -p configs/fig14
for len in ${lens[@]}; do
    python3 configs/victim.py --max_clock=4000000 --policy=${policy} --burst_inv=${len} \
        --cfgname="configs/fig14/len-${len}.toml" --outputdir="fig14"
done
mkdir -p output/fig14
for len in ${lens[@]}; do
    build/Xerxes configs/fig14/len-${len}.toml 1> output/fig14/len-${len}.out 2> output/fig14/len-${len}.err &
done

wait

# Report
for len in ${lens[@]}
do
    python3 output/report.py output/fig14/len-${len}.csv --report=avg_lat > output/fig14/len-${len}_avglat.csv
    python3 output/report.py output/fig14/len-${len}.csv --report=avg_wait_inv > output/fig14/len-${len}_avgwaitinv.csv
done

# Gather reports
mkdir -p output/fig14/summary
for len in ${lens[@]}; do
    cp output/fig14/len-${len}_avglat.csv output/fig14/summary/len-${len}_avglat.csv
    cp output/fig14/len-${len}_avgwaitinv.csv output/fig14/summary/len-${len}_avgwaitinv.csv
    cp output/fig14/len-${len}.err output/fig14/summary/len-${len}.err
done

# Plot fig14 metrics (BW, avg latency via report, avg wait on invalidation) from .err and .csv
python3 output/plot_fig14.py output/fig14
