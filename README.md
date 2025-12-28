# Xerxes

This is a CXL-enabled memory system simulator. 


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

To reproduce the experiments for Figure 15, 16, and 17, use the test scripts in the `AE-scripts` folder:
```bash
cd Xerxes # ensure in the repository root
bash AE-scripts/fig1516.sh # Generates results for Figure 15 and 16 (cf. output/fig1516)
bash AE-scripts/fig17.sh   # Generates results for Figure 17 (cf.output/fig17)
```

After running the test scripts, use the plotting scripts in the `output` folder to generate the figures:
```bash
cd Xerxes # ensure in the repository root
python3 output/plot_fig1516.py # Generates Figure 15 and 16, saving the image to output/fig1516/fig1516.png
python3 output/plot_fig17.py   # Generates Figure 17, saving the image to output/fig17/fig17.png
```


