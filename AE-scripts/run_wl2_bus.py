#!/usr/bin/env python3
"""Run the wl2 workload with the paper's E5 bus topology (Figures 21-23).

This is the wrapper the paper's Section 5.4 numbers were measured with: every
host-side link, leaf-spine link, and leaf-snoop link is a DuplexBus with
delay_per_T = 4 cycles.  It imports run_wl2.py unchanged and replaces only
build_config, so traces, sanity checks, audits and CSVs stay identical.

    XLINK=E5 XLINK_DELAY=4 python3 AE-scripts/run_wl2_bus.py --out-root ... \
        --tag deg_n16_s10000 --mappings global --sharers 16 ...

Every other argument is forwarded to run_wl2.py; see --help there.
"""
import os
import sys
from pathlib import Path

AE = Path(__file__).resolve().parent
sys.path.insert(0, str(AE))

import workload_v2 as wl  # noqa: E402
import run_wl2  # noqa: E402

LINK = os.environ.get("XLINK", "E0")
BUS_DELAY = int(os.environ.get("XLINK_DELAY", "8"))


def _bus(devices, configs, name, delay):
    devices.append(f'{name} = "DuplexBus"')
    configs.append(run_wl2.make_bus(name, delay))


def build_config(*, exp_id, mapping, sf_lines, assoc, policy,
                 trace_files, clean_evict, seed_base, cache_lines,
                 index_hash, max_clock=run_wl2.MAX_CLOCK):
    devices, configs, edges = [], [], []

    for leaf in range(run_wl2.LEAFS):
        devices.append(f'Leaf-{leaf} = "Switch"')
        configs.append(run_wl2.make_switch(f"Leaf-{leaf}", delay=1))
    for spine in range(run_wl2.SPINES):
        devices.append(f'Spine-{spine} = "Switch"')
        configs.append(run_wl2.make_switch(f"Spine-{spine}", delay=0))

    # ---- host side -------------------------------------------------------
    for host in range(run_wl2.TOTAL_HOSTS):
        name = f"Host-{host}"
        devices.append(f'{name} = "Requester"')
        configs.append(run_wl2.make_host(
            name=name, random_seed=seed_base + host,
            trace_file=str(trace_files[host]), clean_evict=clean_evict,
            cache_lines=cache_lines))
        leaf = f"Leaf-{host // run_wl2.HOSTS_PER_LEAF}"
        if LINK in ("E1", "E1z", "E5"):
            d = 0 if LINK == "E1z" else BUS_DELAY
            _bus(devices, configs, f"Bus-H{host}", d)
            edges.append((name, f"Bus-H{host}"))
            edges.append((f"Bus-H{host}", leaf))
        else:
            edges.append((name, leaf))

    # ---- memory side -----------------------------------------------------
    for mem in range(run_wl2.TOTAL_MEMS):
        mem_dev = wl.mem_name(mem)
        devices.append(f'{mem_dev} = "DRAMsim3Interface"')
        configs.append(run_wl2.make_mem(mem_dev))
        snoop = f"Snoop-{mem}"
        devices.append(f'{snoop} = "Snoop"')
        configs.append(run_wl2.make_snoop(snoop, sf_lines, assoc, policy,
                                          index_hash))
        leaf = f"Leaf-{mem // run_wl2.MEMS_PER_LEAF}"
        if LINK in ("E3", "E4", "E5"):
            _bus(devices, configs, f"Bus-S{mem}", BUS_DELAY)
            edges.append((leaf, f"Bus-S{mem}"))
            edges.append((f"Bus-S{mem}", snoop))
        else:
            edges.append((leaf, snoop))
        edges.append((snoop, mem_dev))

    # ---- leaf <-> spine --------------------------------------------------
    for leaf in range(run_wl2.LEAFS):
        for spine in range(run_wl2.SPINES):
            if LINK in ("E2", "E4", "E5"):
                _bus(devices, configs, f"Bus-L{leaf}S{spine}", BUS_DELAY)
                edges.append((f"Leaf-{leaf}", f"Bus-L{leaf}S{spine}"))
                edges.append((f"Bus-L{leaf}S{spine}", f"Spine-{spine}"))
            else:
                edges.append((f"Leaf-{leaf}", f"Spine-{spine}"))

    body = "\n".join(configs) + "\n\n" + wl.address_system_toml(mapping)
    text = run_wl2.TOML_HEADER.format(
        max_clock=max_clock,
        log_name=str(run_wl2.LOG_DIR / f"{exp_id}.csv"),
        edges=run_wl2.toml_edges(edges),
        devices="\n".join(devices),
        device_configs=body,
    )
    path = run_wl2.CONFIG_DIR / f"{exp_id}.toml"
    path.write_text(text, encoding="utf-8")
    return path


run_wl2.build_config = build_config

if __name__ == "__main__":
    raise SystemExit(run_wl2.main())
