#!/bin/bash

traces=("BTree-mini" "liblinear-mini" "redis-mini" "silo-mini" "XSBench-mini")
topos=("chain" "tree" "ring" "spineleaf" "full")

cfg_base="configs/fig12"
out_base="output/fig12"
summary_bw="${out_base}/summary/bw"
summary_lat="${out_base}/summary/avg_lat"

# r-mode: only reports and gather
if [[ "$1" == "r" ]]; then
    mkdir -p "$summary_bw" "$summary_lat"
    echo "[fig12] r-mode: reporting only"
    for topo in ${topos[@]}; do
        for trace in ${traces[@]}; do
            python3 output/report.py ${out_base}/${topo}/${trace}.csv --report=bw > ${out_base}/${topo}/${trace}_bw.csv
            python3 output/report.py ${out_base}/${topo}/${trace}.csv --report=avg_lat > ${out_base}/${topo}/${trace}_avg_lat.csv
            cp ${out_base}/${topo}/${trace}_bw.csv ${summary_bw}/${topo}_${trace}_bw.csv
            cp ${out_base}/${topo}/${trace}_avg_lat.csv ${summary_lat}/${topo}_${trace}_avg_lat.csv
            echo "[fig12][report][${topo}][${trace}] done"
        done
    done
    exit 0
fi

# Build configs
mkdir -p "$cfg_base" "$out_base" "$summary_bw" "$summary_lat"
for topo in ${topos[@]}; do
    mkdir -p ${cfg_base}/${topo} ${out_base}/${topo}
    for trace in ${traces[@]}; do
        python3 configs/trace.py --max_clock=4000000 --trace=${trace} --work=${topo} \
            --cfgname="${cfg_base}/${topo}/${trace}.toml" --outputdir="fig12/${topo}"
    done
done
 
# Run Xerxes
for topo in ${topos[@]}; do
    for trace in ${traces[@]}; do
        timeout -k 10s 150s \
            build/Xerxes ${cfg_base}/${topo}/${trace}.toml \
            1> ${out_base}/${topo}/${trace}.out \
            2> ${out_base}/${topo}/${trace}.err &
    done
    wait
    echo "[fig12][run][${topo}] completed"
done

# Report
for topo in ${topos[@]}; do
    for trace in ${traces[@]}; do
        python3 output/report.py ${out_base}/${topo}/${trace}.csv --report=bw > ${out_base}/${topo}/${trace}_bw.csv
        python3 output/report.py ${out_base}/${topo}/${trace}.csv --report=avg_lat > ${out_base}/${topo}/${trace}_avg_lat.csv
        echo "[fig12][report][${topo}][${trace}] done"
    done
done

# Gather reports
for topo in ${topos[@]}; do
    for trace in ${traces[@]}; do
        cp ${out_base}/${topo}/${trace}_bw.csv ${summary_bw}/${topo}_${trace}_bw.csv
        cp ${out_base}/${topo}/${trace}_avg_lat.csv ${summary_lat}/${topo}_${trace}_avg_lat.csv
    done
done
