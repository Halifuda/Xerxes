#!/bin/bash

traces=("BTree" "liblinear" "redis" "silo" "XSBench")
# traces=("small")

full_dir="fig17/fullbus"
half_dir="fig17/halfbus"

# If arg is 'r', only run report and gather
if [[ "$1" == "r" ]]; then
    mkdir -p output/${full_dir}
    mkdir -p output/${half_dir}
    mkdir -p output/fig17/summary/fullbus
    mkdir -p output/fig17/summary/halfbus
    for trace in ${traces[@]}
    do
        python3 output/report.py output/${full_dir}/${trace}.csv --report=rw > output/${full_dir}/${trace}_rw.csv
        cp output/${full_dir}/${trace}_rw.csv output/fig17/summary/fullbus/${trace}_rw.csv
    done
    for trace in ${traces[@]}
    do
        python3 output/report.py output/${half_dir}/${trace}.csv --report=bw > output/${half_dir}/${trace}_bw.csv
        cp output/${half_dir}/${trace}_bw.csv output/fig17/summary/halfbus/${trace}_bw.csv
    done
    exit 0
fi

# Build config files
mkdir -p configs/fig17/fullbus
mkdir -p configs/fig17/halfbus
mkdir -p output/${full_dir}
mkdir -p output/${half_dir}
mkdir -p output/fig17/summary/fullbus
mkdir -p output/fig17/summary/halfbus

for trace in ${traces[@]}
do
    python3 configs/trace.py --max_clock=4000000 --trace=${trace} --work=fullbus \
        --cfgname="configs/fig17/fullbus/${trace}.toml" --outputdir="${full_dir}"
done

for trace in ${traces[@]}
do
    python3 configs/trace.py --max_clock=4000000 --trace=${trace} --work=halfbus \
        --cfgname="configs/fig17/halfbus/${trace}.toml" --outputdir="${half_dir}"
done
 
# Run Xerxes
for trace in ${traces[@]}
do
    build/Xerxes configs/fig17/fullbus/${trace}.toml 1> output/${full_dir}/${trace}.out 2> output/${full_dir}/${trace}.err &
done

wait

for trace in ${traces[@]}
do
    build/Xerxes configs/fig17/halfbus/${trace}.toml 1> output/${half_dir}/${trace}.out 2> output/${half_dir}/${trace}.err &
done

wait

# Report
for trace in ${traces[@]}
do
    python3 output/report.py output/${full_dir}/${trace}.csv --report=rw > output/${full_dir}/${trace}_rw.csv
done

for trace in ${traces[@]}
do
    python3 output/report.py output/${half_dir}/${trace}.csv --report=bw > output/${half_dir}/${trace}_bw.csv
done

# Gather reports
for trace in ${traces[@]}
do
    cp output/${full_dir}/${trace}_rw.csv output/fig17/summary/fullbus/${trace}_rw.csv
done

for trace in ${traces[@]}
do
    cp output/${half_dir}/${trace}_bw.csv output/fig17/summary/halfbus/${trace}_bw.csv
done
