# Xerxes

This is a CXL-enabled memory system simulator. 

The artifact-evaluation snapshot of the FAST conference version is frozen in the
separate `FAST-AE` branch. This `main` branch tracks the current development
version.


# Build

## DRAM device

The Xerxes uses [DRAMsim3](https://github.com/umd-memsys/DRAMsim3) as the default simulator for endpoint DRAM devices. It is included as a git submodule. When you clone the Xerxes repository, the submodule should be initialized (i.e., using `git submodule init` and `git submodule update`).

## Compile

The Xerxes is built by CMake. Use commands below to build:
```
mkdir -p build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=True
cmake --build build --target Xerxes
```

# Usage

## Generate configuration


We've provided a set of Python scripts in the `configs` folder as the tool and samples to build the TOML config files (cf. `sample-topo.py` and `sample-bus.py`). 

`sample-topo.py`: Focuses on the network topologies (e.g., ring, chain, and full).
`sample-bus.py`: Concentrates on bus-level parameters (e.g., frame size of the DuplexBus).

All these generators accept command-line options. Their typical usage patterns include:



```
python configs/sample-bus.py --scale 4 --ratio 0.25 --log output/sample-bus.csv > configs/sample-bus.toml
python configs/sample-topo.py --scale 16 --topo ring --log output/sample-topo.csv > configs/sample-topo.toml
```

From an implementation perspective, these scripts are wrappers that use the constructors in `mkcfg` (cf. `utils.py` and `devices.py`) to create device instances and connect them together. In particular, consult these constructors to understand the data structures, device models, and helper functions used to construct TOML configurations. Users wishing to extend or customize configuration generation can reuse and adapt these modules.



## Start simulation

The Xerxes requires a TOML file to configure the simulation. The command can be similar to the following:
```
build/Xerxes configs/sample-bus.toml
build/Xerxes configs/sample-topo.toml
```



## Result explanation
Upon successful simulation run, Xerxes emits a per-request log in CSV format to the output file specified in the TOML configuration. Each row corresponds to a single request and records timestamps and per-component latencies used for post-hoc analysis. Example output:

```
id,type,memid,addr,send,arrive,bus_queuing,bus_time,switch_queuing,switch_time,snoop_evict,host_inv,dram_queuing,dram_time,total_time
51,Non-temporal read,19,0,0,139,0,0,0,50,0,0,0,49,139
68,Non-temporal read,20,0,0,139,0,0,0,50,0,0,0,49,139
85,Non-temporal read,21,0,0,139,0,0,0,50,0,0,0,49,139
102,Non-temporal read,22,0,0,139,0,0,0,50,0,0,0,49,139
119,Non-temporal read,23,0,0,139,0,0,0,50,0,0,0,49,139
136,Non-temporal read,24,0,0,139,0,0,0,50,0,0,0,49,139
170,Non-temporal read,26,0,0,139,0,0,0,50,0,0,0,49,139
187,Non-temporal read,27,0,0,139,0,0,0,50,0,0,0,49,139
153,Non-temporal read,25,0,0,139,0,0,0,50,0,0,0,49,139
204,Non-temporal read,28,0,0,139,0,0,0,50,0,0,0,49,139
221,Non-temporal read,29,0,0,139,0,0,0,50,0,0,0,49,139
238,Non-temporal read,30,0,0,139,0,0,0,50,0,0,0,49,139
255,Non-temporal read,31,0,0,139,0,0,0,50,0,0,0,49,139
0,Non-temporal read,16,0,0,139,0,0,0,50,0,0,0,49,139
17,Non-temporal read,17,0,0,139,0,0,0,50,0,0,0,49,139
```


Field descriptions:

- `id`: Identifier assigned to the request within the experiment.
- `type`: Semantic classification of the packet (e.g., non-temporal read, write).
- `memid`: Identifier of the memory endpoint that services the request.
- `addr`: Target address associated with the request.
- `send`: Simulation timestamp when the requester issued/sent the packet.
- `arrive`: Simulation timestamp when the packet reached its final destination or when service was completed at the endpoint.
- `bus_queuing`: Time spent waiting in the bus transmission queue prior to bus transfer.
- `bus_time`: Time consumed by the actual transfer over the bus (frame transmission latency).
- `switch_queuing`: Time spent queued at intermediate switches.
- `switch_time`: Time spent traversing the switches.
- `snoop_evict`: Cycles consumed by snoop-triggered eviction handling.
- `host_inv`: Cycles associated with host-side invalidation handling invoked by coherence events.
- `dram_queuing`: Time spent queued at the DRAM controller before service.
- `dram_time`: Service time at the DRAM device (actual DRAM access latency).
- `total_time`: End-to-end latency observed for the request.

We provide a `report.py` script in the `output` folder to help users analyze simulation results, including statistics for bandwidth (bw), average latency (avg_lat), and other metrics.


# Artifact Evaluation


To streamline the AE process, we provide the `run_all.sh` script in the `AE-scripts` folder, which sequentially executes all AE test scripts and generates the resulting plots, saving them to the `figures` directory.



```bash
# ensure in the repository root
bash AE-scripts/run_all.sh
```


## Reproducing Figures 16--23 (Section 5.2--5.4)

Figures 16--23 are produced by one script from a clean checkout.  The script
runs every cell, then draws the eight EPS files and prints the path each one
must take inside the LaTeX tree.

### Dependencies

Build the simulator as described above; the script expects `build/Xerxes`.
The runners and plotting scripts need Python 3.9+ with `numpy` and
`matplotlib`, plus the DRAMsim3 submodule (the default build already includes
it).  On a machine where the plotting stack lives in a local directory, point
Python at it, for example:

~~~bash
export PYTHONPATH=/path/to/python-libs:$PYTHONPATH
export MPLCONFIGDIR=/tmp/mplconfig
~~~

### Run everything

~~~bash
# from the repository root
python3 AE-scripts/repro_fig16_23.py --jobs 4
~~~

This writes the raw cells under `results/repro_fig16_23/` and the EPS files
under `figures-repro/`.  A full run takes roughly 1--2 hours on a 16-core
machine: the Section 5.2 grid and the three Section 5.4 sweeps dominate,
while the Section 5.3 cells are seconds each.  Re-running the script skips
cells whose `main.csv` / `wl2_results.csv` already exists; pass `--force` to
the individual runners if a cell must be repeated.

To smoke-test the pipeline, run only Figures 19 and 20 with one seed:

~~~bash
python3 AE-scripts/repro_fig16_23.py --figs 19,20 --quick --jobs 4
~~~

To redraw from existing data without rerunning the simulator:

~~~bash
python3 AE-scripts/repro_fig16_23.py --skip-data --fig-root figures-repro
~~~

To copy the EPS files directly into the paper's directory layout:

~~~bash
python3 AE-scripts/repro_fig16_23.py --skip-data \
    --overleaf-dir /path/to/paper
~~~

### What each figure runs

| Figure | Data cells | Driver |
|---|---|---|
| 16 | `ae_extension_results.csv`, section 1.1 | `AE-scripts/run_ae_extension_experiments.py` |
| 17 | 3 layouts x 3 footprints x 3 seeds, 45,000 ops, stride 45 | `AE-scripts/run_footprint_steady.py` |
| 18 | `ae_extension_results.csv`, section 1.3 | `AE-scripts/run_ae_extension_experiments.py` |
| 19 | 5 policies off/on + 512-line-cache / 1,024-line-SF variant | `AE-scripts/run_s53_figures.py` |
| 20 | InvBlk length 1/2/3/4/8 x off/on x 3 seeds | `AE-scripts/run_s53_figures.py` |
| 21 | participant degree 1/4/8/16 x 3 seeds + bare baseline | `AE-scripts/run_s54_figures.py` |
| 22 | SF capacity, 11 points x 3 seeds | `AE-scripts/run_s54_figures.py` |
| 23 | shared-data ratio, 8 points x 3 seeds | `AE-scripts/run_s54_figures.py` |

The Section 5.3 cells use the archived working points in
`configs/repro/s53_policy.toml` (single requester, 3,280-line cache and SF) and
`configs/repro/s53_invblk.toml` (two requesters, 3,280-line cache and SF).
The Section 5.4 cells use `AE-scripts/run_wl2_bus.py`, which rebuilds the E5
bus topology (16 requesters, leaf-spine, `delay_per_T = 4`) around the
unchanged `run_wl2.py` workload.

### LaTeX destinations

Copy the produced EPS files to these paths (or use `--overleaf-dir`):

~~~
fig0609_placement_perfect.eps        -> fig/final-0609-figures/
fig0609_footprint_regimes.eps        -> fig/final-0609-figures/
fig0609_group_size_sensitivity.eps   -> fig/final-0609-figures/
fig_repro_s53_policy.eps             -> fig/test/
fig_repro_s53_invblk.eps             -> fig/test/
fig_repro_s54_degree.eps             -> fig/test/
fig_repro_s54_capacity.eps           -> fig/test/
fig_repro_s54_ratio.eps              -> fig/test/
~~~

Delete any stale `*-eps-converted-to.pdf` next to the EPS files before
rebuilding the paper, otherwise `epstopdf` reuses the cached conversion.

## System topologies (Section 5.1)

To reproduce the experiments for Figure 10, 11, and 12, use the test scripts in the `AE-scripts` folder:
```bash
cd Xerxes # ensure in the repository root
bash AE-scripts/fig10.sh # Generates results for Figure 10 (cf. output/fig10)
bash AE-scripts/fig11.sh # Generates results for Figure 11 (cf. output/fig11)
bash AE-scripts/fig12.sh # Generates results for Figure 12 (cf. output/fig12)
```

After running the test scripts, use the plotting scripts in the `output` folder to generate the figures:
```bash
cd Xerxes # ensure in the repository root
python3 output/plot_fig10.py # Generates Figure 10, saving the image to output/fig10/fig10.png
python3 output/plot_fig11.py # Generates Figure 11, saving the image to output/fig11/fig11.png
python3 output/plot_fig12.py # Generates Figure 12, saving the image to output/fig12/fig12.png
```

## Back-invalidation mechanism (Section 5.2)

To reproduce the experiments for Figure 13 and 14, use the test scripts in the `AE-scripts` folder:
```bash
cd Xerxes # ensure in the repository root
bash AE-scripts/fig13.sh # Generates results for Figure 13 (cf. output/fig13)
bash AE-scripts/fig14.sh # Generates results for Figure 14 (cf. output/fig14)
```

After running the test scripts, use the plotting scripts in the `output` folder to generate the figures:
```bash
cd Xerxes # ensure in the repository root
python3 output/plot_fig13.py # Generates Figure 13, saving the image to output/fig13/fig13.png
python3 output/plot_fig14.py # Generates Figure 14, saving the image to output/fig14/fig14.png
```

## Full duplex transmission (Section 5.3)

To reproduce the experiments for Figure 23, 24, and 25, use the test scripts in the `AE-scripts` folder:
```bash
cd Xerxes # ensure in the repository root
bash AE-scripts/fig2324.sh # Generates results for Figure 23 and 24 (cf. output/fig2324)
bash AE-scripts/fig25.sh   # Generates results for Figure 25 (cf.output/fig25)
```

After running the test scripts, use the plotting scripts in the `output` folder to generate the figures:
```bash
cd Xerxes # ensure in the repository root
python3 output/plot_fig2324.py # Generates Figure 23 and 24, saving the image to output/fig2324/fig2324.png
python3 output/plot_fig25.py   # Generates Figure 25, saving the image to output/fig25/fig25.png
```

## AE extension figures

The AE extension experiments are organized as six figure-level studies over fabric-aware address mapping and distributed snoop-filter behavior. To plot the six AE figures from the summarized CSV files, run:

```bash
cd Xerxes # ensure in the repository root
bash AE-scripts/ae_figures.sh plot
```

This writes the figure files to `output/ae_figures` and copies them to `figures/ae`. To regenerate the experiment configurations and traces without running the simulator, use:

```bash
bash AE-scripts/ae_figures.sh dry-run --profile smoke --sections all
```

To run the experiments and then plot the figures, use:

```bash
bash AE-scripts/ae_figures.sh run --profile full --sections all
```

The plotting script can also be called directly:

```bash
python3 output/plot_ae_figures.py --out-dir output/ae_figures --formats jpg eps
```
