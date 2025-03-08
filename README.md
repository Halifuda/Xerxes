# Xerxes

This is a CXL-enabled memory system simulator. 

# Build

The Xerxes is built by CMake. Use commands below to build:
```
mkdir -p build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=True
cmake --build build --target Xerxes
```

# Usage

The Xerxes requires a TOML file to configure the simulation. The command can be similar to the following:
```
mkdir -p output
build/Xerxes configs/config.toml
```

We've provided a set of Python scripts under `configs` as the tool and samples to build the TOML config files. For example, to build a config file using the `sample-topo.py`, you can run:
```
python configs/sample-topo.py > configs/sample-topo.toml
```

`sample-topo.py`, `sample-bus.py` and `sample-snp.py` are provided.


# DRAMsim3

The Xerxes uses [DRAMsim3](https://github.com/umd-memsys/DRAMsim3) as the default simulator for endpoint DRAM devices. It is included as a git submodule. When you clone the Xerxes repository, the submodule should be initialized (e.g., using `git submodule init` and `git submodule update`).
