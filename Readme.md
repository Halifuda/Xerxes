# Xerxes - anonymous

## DRAMsim3

We leverage DRAMsim3 as the DRAM module simulator. Please use
```bash
git clone https://github.com/umd-memsys/DRAMsim3.git
```
before building Xerxes.

## Build

We use Cmake as our build tool. Please use
```bash
cmake -S . -B build
```
to configure the build directry. Please use
```bash
cmake --build build --target Xerxes
```
to build Xerxes executable.

## Usage

We recommand use-codes-as-scripts methodology. The file `main.cpp` presents an example of configure a CXL system and run simulation. Please remind to rebuild Xerxes after modifying `main.cpp` (will only cost short time thanks to modularized compiling).
