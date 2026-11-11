#!/usr/bin/env python3
"""Self-contained runner for the revised 5.4 workload ("wl2").

Writes, per cell:

    <out-root>[/<tag>]/configs/<exp_id>.toml
    <out-root>[/<tag>]/traces/<exp_id>_host<0..15>.trace
    <out-root>[/<tag>]/sanity/<exp_id>.json
    <out-root>[/<tag>]/logs/<exp_id>.csv          (engine metric dump)
    <out-root>[/<tag>]/wl2_results.csv            (one row per cell)

A dry run redirects everything into <out-root>/_dryrun/ and stops before the
simulator, so it never mixes with real data.  The existing section runner
(run_ae_extension_experiments.py) is not imported and not modified.

The address stream lives in workload_v2.py; this file only adds files, a TOML
config and the invocation.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import subprocess
import sys
import time
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import workload_v2 as wl  # noqa: E402

ROOT = SCRIPT_DIR.parent                 # Xerxes/ (this script lives in AE-scripts/)
XERXES_BIN = ROOT / "build" / "Xerxes"
DEFAULT_OUT_ROOT = "results/rev_54_wl2"
SUMMARY_NAME = "wl2_results.csv"

# Topology and latencies are identical to run_ae_extension_experiments.py.
TOTAL_HOSTS = wl.TOTAL_HOSTS
TOTAL_MEMS = wl.TOTAL_MEMS
LEAFS = wl.LEAFS
SPINES = 2
HOSTS_PER_LEAF = wl.HOSTS_PER_LEAF
MEMS_PER_LEAF = wl.MEMS_PER_LEAF
BLOCK_SIZE = wl.BLOCK_SIZE
DEFAULT_MEM_CAPACITY = 1 << 30
DEFAULT_MEM_DELAY = 40
CACHE_DELAY = 2
HOST_CACHE_LINES = 64
WR_RATIO = 0.0          # inert for trace generators; kept for key parity
MAX_CLOCK = 8_000_000

# When set, an [OracleBus] section with this delay_per_T is appended to every
# generated config.  None keeps the engine default (delay_per_T = 0).
BUS_DELAY: int | None = None

STATES = ("off", "on")
HOST_RE = re.compile(r"^(Host-\d+)(?::\d+)?:([^:]+)$")
SNOOP_RE = re.compile(r"^(Snoop-\d+):([^:]+)$")

TOML_HEADER = """max_clock = {max_clock}
clock_granu = 1
log_level = "INFO"
log_name = "{log_name}"
edges = {edges}

[devices]
{devices}

{device_configs}
"""

# Populated by configure_layout().
BASE_DIR = ROOT / DEFAULT_OUT_ROOT
CONFIG_DIR = BASE_DIR / "configs"
TRACE_DIR = BASE_DIR / "traces"
SANITY_DIR = BASE_DIR / "sanity"
LOG_DIR = BASE_DIR / "logs"
DRAMSIM_OUTPUT_DIR = BASE_DIR / "dramsim_output"
SUMMARY_CSV = BASE_DIR / SUMMARY_NAME


# ---------------------------------------------------------------------------
# CLI.
# ---------------------------------------------------------------------------

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate and run the revised 5.4 workload (wl2)"
    )
    parser.add_argument(
        "--out-root",
        default=DEFAULT_OUT_ROOT,
        help=f"output directory, relative to the repo root or absolute "
             f"(default: {DEFAULT_OUT_ROOT})",
    )
    parser.add_argument(
        "--mappings",
        default=",".join(wl.DEFAULT_MAPPINGS),
        help="comma-separated mapping names: non,leaf,global",
    )
    parser.add_argument(
        "--k",
        default="16,4",
        help="comma-separated sharing-set sizes (the design fixes 16 and 4)",
    )
    parser.add_argument(
        "--sf",
        default=str(wl.DEFAULT_SF_LINES),
        help="comma-separated snoop-filter line counts; the set count is "
             f"sf_lines / --assoc (default: {wl.DEFAULT_SF_LINES})",
    )
    parser.add_argument(
        "--assoc",
        type=int,
        default=wl.SF_ASSOC,
        help="snoop-filter associativity written to every [Snoop-i] block; "
             f"line_num still comes from --sf (default: {wl.SF_ASSOC})",
    )
    parser.add_argument(
        "--line-offset",
        type=int,
        default=0,
        help="shift every wl2 address by this many 64 B lines (default: 0); "
             "must be a multiple of 64 so line %% 16 (endpoint) and line %% 64 "
             "(host cache index) stay fixed, leaving only the SF bucket rotated",
    )
    parser.add_argument(
        "--index-hash",
        action="store_true",
        help="mix the SF line number before the set modulo (writes "
             "index_hash = true into every [Snoop-i]; default: off, keeping "
             "the historical (addr / 64) %% set_num bucket)",
    )
    parser.add_argument(
        "--private-lines",
        type=int,
        default=wl.P_LINES,
        help="private lines per host cycled by the private scan, 1..S_LINES "
             f"(default: {wl.P_LINES})",
    )
    parser.add_argument(
        "--cache-lines",
        type=int,
        default=HOST_CACHE_LINES,
        help=f"per-host cache capacity in 64 B lines (default: {HOST_CACHE_LINES})",
    )
    parser.add_argument(
        "--private-op-type",
        default=wl.WRITE,
        choices=[wl.WRITE, wl.READ],
        help=f"op type of the private runs, W or R (default: {wl.WRITE})",
    )
    parser.add_argument(
        "--policies",
        default="LIFO,FS+LIFO",
        help="comma-separated snoop eviction policies",
    )
    parser.add_argument("--alpha", type=float, default=wl.DEFAULT_ALPHA,
                        help="Zipf exponent of the shared line draw")
    parser.add_argument("--shared-ratio", type=float, default=wl.DEFAULT_SHARED_RATIO,
                        help="fraction of each host's ops that go to the shared "
                             "stream (issued as reads unless "
                             "--shared-write-ratio is positive); ignored when "
                             "--private-ops or --shared-ops is given")
    parser.add_argument("--shared-write-ratio", type=float, default=0.0,
                        help="fraction of the shared-stream ops issued as writes, "
                             "0.0..1.0: each shared op is W with this probability "
                             "and R otherwise.  0.0 (default) reproduces the "
                             "historical all-read shared stream bit-for-bit, while "
                             "a positive value adds a _swr<value> cell-id suffix so "
                             "it cannot overwrite a w=0 cell")
    parser.add_argument("--shared-homing", default="pool",
                        choices=["pool", "leaf", "remote"],
                        help="where the shared slots live: pool (default) draws "
                             "them from the k-slot publisher pool, i.e. from "
                             "whoever shares; leaf always keeps them in the "
                             "caller's own leaf; remote keeps the same four "
                             "windows but homes them one leaf over, so the "
                             "sharers and the footprint stay fixed and only the "
                             "home moves.  pool is the historical behaviour and "
                             "keeps every existing trace bit-identical")
    parser.add_argument("--sharers", type=int, default=None,
                        help="cluster size for the cluster-strided shared layout "
                             "(1, 2, 4, 8, 16): hosts [c*n,(c+1)*n) draw from a "
                             "slice of 512*n lines homed on their own n memory "
                             "endpoints, so total footprint, per-endpoint load "
                             "and tracked lines per SF are constant in n and "
                             "only the number of holders changes.  Requires "
                             "--mappings global.  Unset (default) is the "
                             "historical publisher-slot layout")
    parser.add_argument(
        "--ops-per-host",
        type=int,
        default=wl.OPS_PER_HOST,
        help="total ops per host in ratio mode, split by --shared-ratio "
             f"(default: {wl.OPS_PER_HOST})",
    )
    parser.add_argument(
        "--private-ops",
        type=int,
        default=None,
        help="explicit private ops per host; giving either this or --shared-ops "
             "switches to additive mode: total = private + shared, and "
             "--ops-per-host / --shared-ratio are ignored (default: unset)",
    )
    parser.add_argument(
        "--shared-ops",
        type=int,
        default=None,
        help="explicit shared ops per host in additive mode (default: unset)",
    )
    parser.add_argument("--seed-base", type=int, default=wl.DEFAULT_SEED_BASE,
                        help="host random seed = seed_base + host index")
    parser.add_argument("--timeout", type=int, default=600,
                        help="seconds before a single Xerxes run is killed")
    parser.add_argument("--max-clock", type=int, default=MAX_CLOCK,
                        help="max_clock written to every config; raise it for "
                             "cell whose trace cannot drain inside the window "
                             f"(default: {MAX_CLOCK:,})")
    parser.add_argument(
        "--clean-evict",
        nargs="?",
        const="on",
        default="both",
        choices=["off", "on", "both"],
        help="MemClnEvct state: off, on, or both (default: both)",
    )
    parser.add_argument("--bus-delay", type=int, default=None,
                        help="insert a DuplexBus per endpoint with this delay_per_T")
    parser.add_argument("--tick-stride", type=int, default=1,
                        help="cycles between two issued ops of a host (default 1)")
    parser.add_argument("--bus-width", type=int, default=256,
                        help="DuplexBus width in bytes (default 256)")
    parser.add_argument("--half-duplex", action="store_true",
                        help="set is_full=false on the inserted buses (shared directions)")
    parser.add_argument("--dry-run", action="store_true",
                        help="write configs/traces/sanity under <out-root>/_dryrun "
                             "and do not invoke the simulator")
    parser.add_argument("--tag", default="",
                        help="subdirectory under --out-root, recorded in the CSV")
    return parser.parse_args()


def split_list(raw: str) -> list[str]:
    items = [part.strip() for part in raw.split(",") if part.strip()]
    if not items:
        raise SystemExit(f"empty list in {raw!r}")
    return items


def configure_layout(out_root: str, tag: str, dry_run: bool) -> Path:
    global BASE_DIR, CONFIG_DIR, TRACE_DIR, SANITY_DIR, LOG_DIR, DRAMSIM_OUTPUT_DIR, SUMMARY_CSV
    base = Path(out_root)
    if not base.is_absolute():
        base = ROOT / base
    if tag:
        base = base / tag
    if dry_run:
        base = base / "_dryrun"
    BASE_DIR = base
    CONFIG_DIR = base / "configs"
    TRACE_DIR = base / "traces"
    SANITY_DIR = base / "sanity"
    LOG_DIR = base / "logs"
    DRAMSIM_OUTPUT_DIR = base / "dramsim_output"
    SUMMARY_CSV = base / SUMMARY_NAME
    for path in (BASE_DIR, CONFIG_DIR, TRACE_DIR, SANITY_DIR, LOG_DIR, DRAMSIM_OUTPUT_DIR):
        path.mkdir(parents=True, exist_ok=True)
    return base


# ---------------------------------------------------------------------------
# TOML blocks (key order matches the existing runner exactly).
# ---------------------------------------------------------------------------

def make_host(
    name: str,
    random_seed: int,
    trace_file: str,
    clean_evict: bool,
    cache_lines: int,
) -> str:
    return f"""[{name}]
q_capacity = 32
cache_capacity = {cache_lines}
cache_delay = {CACHE_DELAY}
issue_delay = 0
coherent = true
burst_size = 1
block_size = {BLOCK_SIZE}
interleave_type = "trace"
interleave_param = 0
hot_req_ratio = 1.0
hot_region_ratio = 1.0
trace_file = "{trace_file}"
random_seed = {random_seed}
hpa_base = 0
hpa_size = {wl.HPA_SIZE}
wr_ratio = {WR_RATIO}
clean_evict_notify = {str(clean_evict).lower()}"""


def make_mem(name: str) -> str:
    return (
        f"[{name}]\n"
        "tick_per_clock = 1\n"
        f"process_time = {DEFAULT_MEM_DELAY}\n"
        "start = 0\n"
        f"capacity = {DEFAULT_MEM_CAPACITY}\n"
        "wr_ratio = 0.0\n"
        'config_file = "DRAMsim3/configs/DDR4_8Gb_x8_3200.ini"\n'
        f'output_dir = "{DRAMSIM_OUTPUT_DIR}"'
    )


BUS_FULL_DUPLEX = True
BUS_WIDTH = 256  # bytes per transfer unit


def make_bus(name: str, delay_per_t: int) -> str:
    return f"""[{name}]
is_full = {str(BUS_FULL_DUPLEX).lower()}
half_rev_time = 100
delay_per_T = {delay_per_t}
width = {BUS_WIDTH}
framing_time = 20
frame_size = 32"""


def make_switch(name: str, delay: int) -> str:
    return f"""[{name}]
delay = {delay}"""


def make_snoop(
    name: str, line_num: int, assoc: int, eviction: str, index_hash: bool
) -> str:
    return (
        f"[{name}]\n"
        f"line_num = {line_num}\n"
        f"assoc = {assoc}\n"
        "max_burst_inv = 8\n"
        f"ranges = [[0, {wl.HPA_SIZE}]]\n"
        f'eviction = "{eviction}"\n'
        f"index_hash = {str(index_hash).lower()}"
    )


def toml_edges(edge_list: list[tuple[str, str]]) -> str:
    lines = ["["]
    for src, dst in edge_list:
        lines.append(f'    ["{src}", "{dst}"],')
    lines.append("]")
    return "\n".join(lines)


def leaf_of_host(host: int) -> int:
    return host // HOSTS_PER_LEAF


def rel(path: Path) -> str:
    """Repo-relative when possible, absolute otherwise (out-root may be elsewhere)."""
    try:
        return str(path.relative_to(ROOT))
    except ValueError:
        return str(path)


def build_config(
    *,
    exp_id: str,
    mapping: str,
    sf_lines: int,
    assoc: int,
    policy: str,
    trace_files: list[Path],
    clean_evict: bool,
    seed_base: int,
    cache_lines: int,
    index_hash: bool,
    max_clock: int = MAX_CLOCK,
) -> Path:
    devices: list[str] = []
    configs: list[str] = []
    edges: list[tuple[str, str]] = []

    for leaf in range(LEAFS):
        devices.append(f'Leaf-{leaf} = "Switch"')
        configs.append(make_switch(f"Leaf-{leaf}", delay=1))
    for spine in range(SPINES):
        devices.append(f'Spine-{spine} = "Switch"')
        configs.append(make_switch(f"Spine-{spine}", delay=0))

    for host in range(TOTAL_HOSTS):
        name = f"Host-{host}"
        devices.append(f'{name} = "Requester"')
        configs.append(
            make_host(
                name=name,
                random_seed=seed_base + host,
                trace_file=str(trace_files[host]),
                clean_evict=clean_evict,
                cache_lines=cache_lines,
            )
        )
        if BUS_DELAY is None:
            edges.append((name, f"Leaf-{leaf_of_host(host)}"))
        else:
            bus = f"Bus-{host}"
            devices.append(f'{bus} = "DuplexBus"')
            configs.append(make_bus(bus, BUS_DELAY))
            edges.append((name, bus))
            edges.append((bus, f"Leaf-{leaf_of_host(host)}"))

    for mem in range(TOTAL_MEMS):
        mem_dev = wl.mem_name(mem)
        devices.append(f'{mem_dev} = "DRAMsim3Interface"')
        configs.append(make_mem(mem_dev))
        snoop = f"Snoop-{mem}"
        devices.append(f'{snoop} = "Snoop"')
        configs.append(
            make_snoop(snoop, sf_lines, assoc, policy, index_hash)
        )
        leaf = mem // MEMS_PER_LEAF
        edges.append((f"Leaf-{leaf}", snoop))
        edges.append((snoop, mem_dev))

    for leaf in range(LEAFS):
        for spine in range(SPINES):
            edges.append((f"Leaf-{leaf}", f"Spine-{spine}"))

    body = "\n".join(configs) + "\n\n" + wl.address_system_toml(mapping)
    cfg_text = TOML_HEADER.format(
        max_clock=max_clock,
        log_name=str(LOG_DIR / f"{exp_id}.csv"),
        edges=toml_edges(edges),
        devices="\n".join(devices),
        device_configs=body,
    )
    cfg_path = CONFIG_DIR / f"{exp_id}.toml"
    cfg_path.write_text(cfg_text, encoding="utf-8")
    return cfg_path


# ---------------------------------------------------------------------------
# Traces and sanity.
# ---------------------------------------------------------------------------

def write_traces(exp_id: str, workload: wl.Workload) -> list[Path]:
    paths: list[Path] = []
    for host, ops in enumerate(workload.hosts):
        path = TRACE_DIR / f"{exp_id}_host{host}.trace"
        path.write_text(
            "\n".join(f"{addr:x} {kind} {tick}" for addr, kind, tick in ops),
            encoding="utf-8",
        )
        paths.append(path)
    return paths


def build_sanity(
    *,
    exp_id: str,
    workload: wl.Workload,
    mapping: str,
    sf_lines: int,
    assoc: int,
) -> dict:
    set_count = wl.snoop_set_count(sf_lines, assoc)
    # Private/shared is a property of the address region, not of the op kind:
    # with --private-op-type R both streams are reads, so a kind-based split
    # would silently count every private op as shared.  The region test runs on
    # the pre-offset address, which is the layout generate() actually used.
    offset_bytes = workload.meta.get("line_offset", 0) * wl.BLOCK_SIZE
    per_endpoint_ops = {wl.mem_name(i): 0 for i in range(TOTAL_MEMS)}
    per_endpoint_lines: dict[str, set[int]] = {wl.mem_name(i): set() for i in range(TOTAL_MEMS)}
    per_endpoint_sets: dict[str, set[int]] = {wl.mem_name(i): set() for i in range(TOTAL_MEMS)}
    per_host: list[dict] = []
    total_ops = 0
    total_lines: set[int] = set()
    max_addr = 0

    for host, ops in enumerate(workload.hosts):
        private_ops = shared_ops = 0
        host_lines: set[int] = set()
        for addr, _kind, _tick in ops:
            endpoint = wl.layout_endpoint(addr, mapping)
            if not endpoint.valid:
                raise SystemExit(
                    f"{exp_id}: host {host} address 0x{addr:x} is unmapped"
                )
            name = wl.mem_name(endpoint.mem)
            per_endpoint_ops[name] += 1
            per_endpoint_lines[name].add(addr)
            per_endpoint_sets[name].add(wl.snoop_set_of(addr, sf_lines, assoc))
            host_lines.add(addr)
            total_lines.add(addr)
            max_addr = max(max_addr, addr)
            if addr - offset_bytes < wl.SHARED_BASE:
                private_ops += 1
            else:
                shared_ops += 1
        total_ops += len(ops)
        per_host.append(
            {
                "host": f"Host-{host}",
                "ops": len(ops),
                "private_ops": private_ops,
                "shared_ops": shared_ops,
                "distinct_lines": len(host_lines),
            }
        )

    shares = {
        name: (count / total_ops if total_ops else 0.0)
        for name, count in per_endpoint_ops.items()
    }
    endpoints = [
        {
            "mem": name,
            "ops": per_endpoint_ops[name],
            "share": shares[name],
            "distinct_lines": len(per_endpoint_lines[name]),
            "distinct_sets": len(per_endpoint_sets[name]),
        }
        for name in (wl.mem_name(i) for i in range(TOTAL_MEMS))
    ]

    return {
        "exp_id": exp_id,
        "workload_version": workload.meta["workload_version"],
        "mapping": mapping,
        "k": workload.meta["k"],
        "alpha": workload.meta["alpha"],
        "shared_ratio": workload.meta["shared_ratio"],
        "seed_base": workload.meta["seed_base"],
        "sf_lines": sf_lines,
        "assoc": assoc,
        "line_offset": workload.meta.get("line_offset", 0),
        "set_count": set_count,
        "total_ops": total_ops,
        "hpa_size": wl.HPA_SIZE,
        "max_addr": max_addr,
        "max_addr_within_hpa": max_addr < wl.HPA_SIZE,
        "per_endpoint": endpoints,
        "per_host": per_host,
        "totals": {
            "distinct_lines": len(total_lines),
            "distinct_sets": len(
                {wl.snoop_set_of(addr, sf_lines, assoc) for addr in total_lines}
            ),
            "max_endpoint_share": max(shares.values()),
            "min_endpoint_share": min(shares.values()),
            "min_endpoint_distinct_lines": min(
                len(per_endpoint_lines[name]) for name in per_endpoint_lines
            ),
            "min_endpoint_distinct_sets": min(
                len(per_endpoint_sets[name]) for name in per_endpoint_sets
            ),
            "max_endpoint_ops": max(per_endpoint_ops.values()),
            "min_endpoint_ops": min(per_endpoint_ops.values()),
        },
    }


def sanity_columns(sanity: dict) -> dict:
    columns: dict[str, object] = {
        "sanity_total_ops": sanity["total_ops"],
        "sanity_max_addr": sanity["max_addr"],
        "sanity_hpa_ok": "true" if sanity["max_addr_within_hpa"] else "false",
        "sanity_set_count": sanity["set_count"],
        "sanity_max_endpoint_share": f"{sanity['totals']['max_endpoint_share']:.6f}",
        "sanity_min_endpoint_share": f"{sanity['totals']['min_endpoint_share']:.6f}",
        "sanity_min_endpoint_distinct_lines": sanity["totals"]["min_endpoint_distinct_lines"],
        "sanity_min_endpoint_distinct_sets": sanity["totals"]["min_endpoint_distinct_sets"],
        "sanity_distinct_lines": sanity["totals"]["distinct_lines"],
        "sanity_distinct_sets": sanity["totals"]["distinct_sets"],
    }
    columns["workload_max_mem_share"] = columns["sanity_max_endpoint_share"]
    columns["workload_min_mem_share"] = columns["sanity_min_endpoint_share"]
    for entry in sanity["per_endpoint"]:
        columns[f"workload_mem_ops_{entry['mem']}"] = entry["ops"]
        columns[f"workload_mem_share_{entry['mem']}"] = f"{entry['share']:.6f}"
        columns[f"sanity_mem_lines_{entry['mem']}"] = entry["distinct_lines"]
        columns[f"sanity_mem_sets_{entry['mem']}"] = entry["distinct_sets"]
    return columns


def print_sanity_report(sanity: dict) -> None:
    totals = sanity["totals"]
    print(f"  mapping={sanity['mapping']} k={sanity['k']} alpha={sanity['alpha']} "
          f"sf={sanity['sf_lines']} (assoc {sanity['assoc']}, "
          f"{sanity['set_count']} sets)")
    print(f"  total ops={sanity['total_ops']:,}  max addr=0x{sanity['max_addr']:x} "
          f"({sanity['max_addr']:,} < {sanity['hpa_size']:,}) "
          f"hpa_ok={sanity['max_addr_within_hpa']}")
    host_ops = sorted({entry["ops"] for entry in sanity["per_host"]})
    print(f"  per-host ops: {host_ops} (16 hosts), "
          f"private/shared per host = "
          f"{sanity['per_host'][0]['private_ops']}/"
          f"{sanity['per_host'][0]['shared_ops']}")
    header = f"  {'endpoint':10s} {'ops':>7s} {'share':>7s} {'lines':>6s} {'sets':>5s}"
    print(header)
    print("  " + "-" * (len(header) - 2))
    for entry in sanity["per_endpoint"]:
        print(f"  {entry['mem']:10s} {entry['ops']:7d} "
              f"{entry['share'] * 100:6.2f}% {entry['distinct_lines']:6d} "
              f"{entry['distinct_sets']:5d}")
    print(f"  share range {totals['min_endpoint_share'] * 100:.2f}%.."
          f"{totals['max_endpoint_share'] * 100:.2f}% (ideal 6.25%)")
    print(f"  distinct lines per endpoint {totals['min_endpoint_distinct_lines']}.."
          f"{max(e['distinct_lines'] for e in sanity['per_endpoint'])}, "
          f"set coverage {totals['min_endpoint_distinct_sets']}.."
          f"{max(e['distinct_sets'] for e in sanity['per_endpoint'])} "
          f"of {sanity['set_count']}")


# ---------------------------------------------------------------------------
# Metrics.
# ---------------------------------------------------------------------------

def parse_summary(csv_path: Path) -> dict[str, float]:
    metrics: dict[str, float] = {}
    if not csv_path.exists():
        return metrics
    with csv_path.open() as fh:
        in_summary = False
        for raw_line in fh:
            line = raw_line.strip()
            if line == "metric,value":
                in_summary = True
                continue
            if in_summary:
                if "," not in line:
                    break
                key, value = line.split(",", 1)
                try:
                    metrics[key] = float(value)
                except ValueError:
                    continue
    return metrics


def sum_host(row: dict[str, object], suffix: str) -> float:
    """Sum one per-host metric across all 16 requesters."""
    return sum(
        float(value)
        for key, value in row.items()
        if key.startswith("Host-") and key.endswith(suffix) and value not in ("", None)
    )


def add_derived_metrics(row: dict[str, object]) -> None:
    """Same derived columns as the existing section runner."""
    host_metric_cols: dict[str, list[str]] = {}
    snoop_metric_cols: dict[str, list[str]] = {}

    for key in list(row.keys()):
        if not isinstance(key, str):
            continue
        host_match = HOST_RE.match(key)
        if host_match:
            _, metric = host_match.groups()
            if key.count(":") == 1:
                host_metric_cols.setdefault(metric, []).append(key)
            continue
        snoop_match = SNOOP_RE.match(key)
        if snoop_match:
            _, metric = snoop_match.groups()
            snoop_metric_cols.setdefault(metric, []).append(key)

    req_cols = host_metric_cols.get("req_count", [])
    if "bw_gbps" in host_metric_cols:
        row["host_sum_bw_gbps"] = sum(
            float(row[col]) for col in host_metric_cols["bw_gbps"] if row.get(col) != ""
        )
    if "avg_latency_ns" in host_metric_cols:
        if req_cols:
            weighted_numer = 0.0
            weighted_denom = 0.0
            for req_col, lat_col in zip(
                sorted(req_cols), sorted(host_metric_cols["avg_latency_ns"])
            ):
                req = float(row.get(req_col, 0.0) or 0.0)
                lat = float(row.get(lat_col, 0.0) or 0.0)
                weighted_numer += req * lat
                weighted_denom += req
            row["host_weighted_avg_latency_ns"] = (
                weighted_numer / weighted_denom if weighted_denom else ""
            )
        else:
            lats = [
                float(row[col])
                for col in host_metric_cols["avg_latency_ns"]
                if row.get(col) != ""
            ]
            row["host_weighted_avg_latency_ns"] = sum(lats) / len(lats) if lats else ""
    if "avg_evict_wait_ns" in host_metric_cols:
        waits = [
            float(row[col])
            for col in host_metric_cols["avg_evict_wait_ns"]
            if row.get(col) != ""
        ]
        row["host_mean_avg_evict_wait_ns"] = sum(waits) / len(waits) if waits else ""

    for metric, cols in snoop_metric_cols.items():
        values = [float(row[col]) for col in cols if row.get(col) != ""]
        if not values:
            continue
        if metric.startswith("avg_"):
            row[f"snoop_mean_{metric}"] = sum(values) / len(values)
        else:
            row[f"snoop_sum_{metric}"] = sum(values)


# ---------------------------------------------------------------------------
# Cells.
# ---------------------------------------------------------------------------

def policy_slug(policy: str) -> str:
    return policy.lower().replace("+", "p")


def exp_id_for(
    mapping: str,
    k: int,
    sf: int,
    policy: str,
    state: str,
    assoc: int,
    line_offset: int,
    private_lines: int,
    cache_lines: int,
    private_op_type: str,
    index_hash: bool,
    ops_per_host: int = wl.OPS_PER_HOST,
    private_ops: int | None = None,
    shared_ops: int | None = None,
    shared_write_ratio: float = 0.0,
    shared_homing: str = "pool",
    sharers: int | None = None,
) -> str:
    """Cell id; the override suffixes appear only when non-default.

    Defaults (assoc=4, offset=0, P=257, cache=64, private op W, index hash
    off, shared write ratio 0) keep the historic id byte-for-byte so existing
    result trees still line up.  The suffixes keep cells that share sf but
    differ in ways, set rotation, private footprint, host cache size, private
    op kind, set-index hashing or shared write ratio from overwriting each
    other's traces.
    """
    suffix = ""
    if assoc != wl.SF_ASSOC:
        suffix += f"_a{assoc}"
    if line_offset:
        suffix += f"_o{line_offset}"
    if private_lines != wl.P_LINES:
        suffix += f"_pl{private_lines}"
    if cache_lines != HOST_CACHE_LINES:
        suffix += f"_cl{cache_lines}"
    if private_op_type != wl.WRITE:
        suffix += f"_pt{private_op_type}"
    if index_hash:
        suffix += "_ih"
    if shared_write_ratio > 0.0:
        # Only a positive ratio changes the stream; 0.0 must stay suffix-free
        # so every pre-existing cell id keeps resolving to the same trace.
        suffix += f"_swr{shared_write_ratio:g}"
    if shared_homing != "pool":
        suffix += f"_sh{shared_homing}"
    if sharers is not None:
        suffix += f"_n{sharers}"
    if private_ops is not None or shared_ops is not None:
        # Additive mode: the explicit counts are the identity of the stream,
        # so they replace --ops-per-host in the id instead of stacking on it.
        suffix += "_po{}_so{}".format(
            0 if private_ops is None else private_ops,
            0 if shared_ops is None else shared_ops,
        )
    elif ops_per_host != wl.OPS_PER_HOST:
        suffix += f"_op{ops_per_host}"
    return f"wl2_{mapping}_k{k}_sf{sf}{suffix}_{policy_slug(policy)}_{state}"


def run_cell(
    *,
    exp_id: str,
    mapping: str,
    k: int,
    sf: int,
    policy: str,
    state: str,
    tag: str,
    workload: wl.Workload,
    sanity: dict,
    cache_lines: int,
    index_hash: bool,
    timeout: int,
    dry_run: bool,
    max_clock: int,
) -> dict[str, object]:
    trace_files = write_traces(exp_id, workload)
    sanity_path = SANITY_DIR / f"{exp_id}.json"
    sanity_path.write_text(
        json.dumps(
            {
                "exp_id": exp_id,
                "index_hash": index_hash,
                "workload": workload.meta,
                "sanity": sanity,
            },
            indent=2,
        ),
        encoding="utf-8",
    )
    cfg_path = build_config(
        exp_id=exp_id,
        mapping=mapping,
        sf_lines=sf,
        assoc=sanity["assoc"],
        policy=policy,
        trace_files=trace_files,
        clean_evict=(state == "on"),
        seed_base=workload.meta["seed_base"],
        cache_lines=cache_lines,
        index_hash=index_hash,
        max_clock=max_clock,
    )

    # The expected op count belongs to the generated workload, not to the
    # module constant: --ops-per-host and the additive private/shared counts
    # both move it, and auditing against a stale 144,000 would fail every
    # cell whose budget is not the historical one.
    expected_ops = int(workload.meta["total_ops"])

    row: dict[str, object] = {
        "exp_id": exp_id,
        "workload_version": workload.meta["workload_version"],
        "tag": tag,
        "mapping": mapping,
        "k": k,
        "alpha": workload.meta["alpha"],
        "shared_ratio": workload.meta["shared_ratio"],
        "shared_write_ratio": workload.meta["shared_write_ratio"],
        "shared_homing": workload.meta.get("shared_homing", "pool"),
        "sharers": workload.meta.get("sharers", ""),
        "shared_ops": workload.meta["shared_ops"],
        "private_ops": workload.meta["private_ops"],
        "private_pre_ops": workload.meta["private_pre_ops"],
        "private_post_ops": workload.meta["private_post_ops"],
        "sf_lines": sf,
        "sf_assoc": sanity["assoc"],
        "line_offset": sanity["line_offset"],
        "index_hash": "true" if index_hash else "false",
        "policy": policy,
        "state": state,
        "clean_evict_notify": "true" if state == "on" else "false",
        "seed_base": workload.meta["seed_base"],
        "cache_capacity_lines": cache_lines,
        "hpa_size": wl.HPA_SIZE,
        "s_lines": wl.S_LINES,
        "s_bytes": wl.S_BYTES,
        "p_lines": workload.meta["p_lines"],
        "private_lines": workload.meta["p_lines"],
        "private_op_type": workload.meta["private_op_type"],
        "ops_per_host": workload.meta["ops_per_host"],
        "total_ops_expected": expected_ops,
        "config_toml": rel(cfg_path),
        "trace_prefix": rel(TRACE_DIR / f"{exp_id}_host*.trace"),
        "sanity_json": rel(sanity_path),
        "notes": "",
    }
    row.update(sanity_columns(sanity))

    if dry_run:
        row["run_status"] = "dry_run"
        return row

    print(f"[{exp_id}] ", end="", flush=True)
    log_path = LOG_DIR / f"{exp_id}.csv"
    start = time.time()
    try:
        result = subprocess.run(
            [str(XERXES_BIN), str(cfg_path)],
            capture_output=True,
            text=True,
            timeout=timeout,
            cwd=ROOT,
        )
        ok = result.returncode == 0
        output = (result.stdout + "\n" + result.stderr).strip()
    except subprocess.TimeoutExpired:
        ok = False
        output = f"timeout after {timeout}s"
    elapsed = time.time() - start
    row["elapsed_sec"] = round(elapsed, 2)
    if not ok:
        print(f"FAIL ({elapsed:.1f}s)")
        row["run_status"] = "failed"
        row["failure_excerpt"] = output[-4000:]
        return row
    print(f"OK ({elapsed:.1f}s)")
    row["run_status"] = "ok"
    row.update(parse_summary(log_path))
    add_derived_metrics(row)
    # First audit criterion: every trace op must be seen exactly once as either
    # an issue (req_count) or a cache hit (cache_hit_count).
    processed = sum_host(row, ":req_count") + sum_host(row, ":cache_hit_count")
    row["processed_ops"] = int(round(processed))
    row["processed_ok"] = "true" if int(round(processed)) == expected_ops else "false"
    row["unexpected_pkt"] = int(
        round(
            sum(
                float(value)
                for key, value in row.items()
                if key.startswith("Mem-") and key.endswith(":unexpected_pkt")
                and value not in ("", None)
            )
        )
    )
    audit_ok = row["processed_ok"] == "true" and row["unexpected_pkt"] == 0
    row["audit_ok"] = "true" if audit_ok else "false"
    # Fail loudly, never silently: an audited cell has to account for all
    # 144,000 trace ops and must not raise a single unexpected packet.
    if audit_ok:
        print(f"  audit: processed {row['processed_ops']:,}/{expected_ops:,}, "
              "unexpected_pkt 0")
    else:
        print(f"  AUDIT FAIL: processed {row['processed_ops']:,}/{expected_ops:,} "
              f"(processed_ok={row['processed_ok']}), "
              f"unexpected_pkt={row['unexpected_pkt']}")
    return row


def load_existing() -> dict[str, dict[str, object]]:
    if not SUMMARY_CSV.exists():
        return {}
    with SUMMARY_CSV.open(newline="") as fh:
        return {
            str(row.get("exp_id", "")): row
            for row in csv.DictReader(fh)
            if row.get("exp_id")
        }


ORDERED_META = [
    "exp_id",
    "workload_version",
    "tag",
    "mapping",
    "k",
    "alpha",
    "shared_ratio",
    "shared_ops",
    "private_ops",
    "private_pre_ops",
    "private_post_ops",
    "sf_lines",
    "sf_assoc",
    "line_offset",
    "index_hash",
    "policy",
    "state",
    "clean_evict_notify",
    "seed_base",
    "cache_capacity_lines",
    "hpa_size",
    "s_lines",
    "s_bytes",
    "p_lines",
    "private_lines",
    "private_op_type",
    "ops_per_host",
    "total_ops_expected",
    "run_status",
    "elapsed_sec",
    "processed_ops",
    "processed_ok",
    "unexpected_pkt",
    "audit_ok",
    "sanity_total_ops",
    "sanity_max_addr",
    "sanity_hpa_ok",
    "sanity_set_count",
    "sanity_max_endpoint_share",
    "sanity_min_endpoint_share",
    "sanity_min_endpoint_distinct_lines",
    "sanity_min_endpoint_distinct_sets",
    "sanity_distinct_lines",
    "sanity_distinct_sets",
    "workload_max_mem_share",
    "workload_min_mem_share",
    "sanity_json",
    "config_toml",
    "trace_prefix",
    "failure_excerpt",
    "notes",
    # Appended, never inserted: every pre-existing column keeps its position,
    # so old summaries stay readable and new ones gain one trailing field.
    "shared_write_ratio",
    "shared_homing",
    "sharers",
]


def write_results(rows: list[dict[str, object]]) -> int:
    merged = load_existing()
    for row in rows:
        merged[str(row["exp_id"])] = row
    rows_to_write = list(merged.values())
    extras = sorted(
        {
            key
            for row in rows_to_write
            for key in row.keys()
            if key not in ORDERED_META
        }
    )
    with SUMMARY_CSV.open("w", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=ORDERED_META + extras)
        writer.writeheader()
        for row in rows_to_write:
            writer.writerow({key: row.get(key, "") for key in ORDERED_META + extras})
    return len(rows_to_write)


# ---------------------------------------------------------------------------
# Main.
# ---------------------------------------------------------------------------

def main() -> int:
    args = parse_args()
    global BUS_DELAY, BUS_FULL_DUPLEX, BUS_WIDTH
    BUS_DELAY = args.bus_delay
    BUS_FULL_DUPLEX = not args.half_duplex
    BUS_WIDTH = args.bus_width
    mappings = split_list(args.mappings)
    for mapping in mappings:
        if mapping not in wl.MAPPINGS:
            raise SystemExit(f"unknown mapping {mapping!r}; expected {sorted(wl.MAPPINGS)}")
    if args.sharers is not None and any(m != "global" for m in mappings):
        raise SystemExit(
            "--sharers lays the shared region out as a 16-way stripe, so it "
            f"only decodes as intended under --mappings global (got {mappings})"
        )
    ks = [int(value) for value in split_list(args.k)]
    sfs = [int(value) for value in split_list(args.sf)]
    policies = split_list(args.policies)
    states = list(STATES) if args.clean_evict == "both" else [args.clean_evict]

    # --assoc changes the SF ways but not --sf: the set count is sf / assoc.
    assoc = args.assoc
    if assoc <= 0:
        raise SystemExit(f"--assoc must be positive, got {assoc}")
    for sf in sfs:
        if sf % assoc != 0:
            raise SystemExit(
                f"--sf {sf} is not a multiple of --assoc {assoc}: "
                "sets = line_num / assoc must be an integer"
            )

    # --line-offset rotates the SF buckets: a whole number of lines shifts
    # set = line % sets while line % 64 (host cache index) and line % 16
    # (global endpoint) stay put.  That invariant only holds for the global
    # mapping, so refuse to shift non/leaf rather than silently change them.
    line_offset = args.line_offset
    if line_offset < 0 or line_offset % 64 != 0:
        raise SystemExit(
            f"--line-offset must be a non-negative multiple of 64 lines, "
            f"got {line_offset}"
        )
    if line_offset and any(mapping != "global" for mapping in mappings):
        raise SystemExit(
            "--line-offset preserves the endpoint only for mapping=global "
            f"(got {mappings}); a shift would remap non/leaf endpoints"
        )

    private_lines = args.private_lines
    if not 1 <= private_lines <= wl.S_LINES:
        raise SystemExit(
            f"--private-lines must be in [1, {wl.S_LINES}], got {private_lines}"
        )
    cache_lines = args.cache_lines
    if cache_lines <= 0:
        raise SystemExit(f"--cache-lines must be positive, got {cache_lines}")
    private_op_type = args.private_op_type
    if private_op_type not in (wl.READ, wl.WRITE):
        raise SystemExit(
            f"--private-op-type must be {wl.WRITE} or {wl.READ}, "
            f"got {private_op_type!r}"
        )

    # Additive mode: an explicit private/shared count fixes the per-host total
    # as their sum and overrides both --ops-per-host and --shared-ratio.
    explicit_ops = args.private_ops is not None or args.shared_ops is not None
    if explicit_ops:
        for name, value in (("--private-ops", args.private_ops),
                            ("--shared-ops", args.shared_ops)):
            if value is not None and value < 0:
                raise SystemExit(f"{name} must be non-negative, got {value}")
        explicit_private = 0 if args.private_ops is None else args.private_ops
        explicit_shared = 0 if args.shared_ops is None else args.shared_ops
        if explicit_private + explicit_shared <= 0:
            raise SystemExit(
                "--private-ops + --shared-ops must be positive, got "
                f"{explicit_private} + {explicit_shared}"
            )
    elif args.ops_per_host <= 0:
        raise SystemExit(
            f"--ops-per-host must be positive, got {args.ops_per_host}"
        )
    if args.max_clock <= 0:
        raise SystemExit(f"--max-clock must be positive, got {args.max_clock}")
    shared_write_ratio = args.shared_write_ratio
    if not 0.0 <= shared_write_ratio <= 1.0:
        raise SystemExit(
            f"--shared-write-ratio must be in [0, 1], got {shared_write_ratio}"
        )
    shared_homing = args.shared_homing
    sharers = args.sharers

    base = configure_layout(args.out_root, args.tag, args.dry_run)
    workloads = {
        k: wl.generate(
            k=k,
            alpha=args.alpha,
            shared_ratio=args.shared_ratio,
            seed_base=args.seed_base,
            line_offset=line_offset,
            private_lines=private_lines,
            private_op_type=private_op_type,
            ops_per_host=args.ops_per_host,
            private_ops=args.private_ops,
            shared_ops=args.shared_ops,
            tick_stride=args.tick_stride,
            shared_write_ratio=shared_write_ratio,
            shared_homing=shared_homing,
            sharers=sharers,
        )
        for k in ks
    }
    # cache_lines lives on the host side, not in the generated stream, but the
    # sanity JSON records the whole run configuration, so annotate the meta.
    for workload in workloads.values():
        workload.meta["cache_lines"] = cache_lines
    print(
        f"wl2: {len(mappings)} mapping(s) x {len(ks)} k x {len(sfs)} sf x "
        f"{len(policies)} policy x {len(states)} state = "
        f"{len(mappings) * len(ks) * len(sfs) * len(policies) * len(states)} cells"
    )
    print(f"assoc={assoc}, line_offset={line_offset} lines "
          f"({line_offset * wl.BLOCK_SIZE} B), "
          f"sets={{{', '.join(str(sf // assoc) for sf in sfs)}}}, "
          f"P={private_lines}, cache={cache_lines}, private_op={private_op_type}, "
          f"index_hash={args.index_hash}, "
          f"shared_write_ratio={shared_write_ratio:g}, "
          f"shared_homing={shared_homing}, sharers={sharers}")
    if explicit_ops:
        total_per_host = explicit_private + explicit_shared
        print(
            f"ops budget: explicit --private-ops {explicit_private} + "
            f"--shared-ops {explicit_shared} = {total_per_host:,} per host "
            f"({total_per_host * TOTAL_HOSTS:,} total); --ops-per-host "
            f"({args.ops_per_host:,}) and --shared-ratio ({args.shared_ratio}) "
            "ignored"
        )
    print(f"output root: {base}")

    rows: list[dict[str, object]] = []
    for mapping in mappings:
        for k in ks:
            workload = workloads[k]
            for sf in sfs:
                sanity = build_sanity(
                    exp_id="probe",
                    workload=workload,
                    mapping=mapping,
                    sf_lines=sf,
                    assoc=assoc,
                )
                for policy in policies:
                    for state in states:
                        exp_id = exp_id_for(
                            mapping, k, sf, policy, state, assoc, line_offset,
                            private_lines, cache_lines, private_op_type,
                            args.index_hash,
                            ops_per_host=args.ops_per_host,
                            private_ops=args.private_ops,
                            shared_ops=args.shared_ops,
                            shared_write_ratio=shared_write_ratio,
                            shared_homing=shared_homing,
                            sharers=sharers,
                        )
                        sanity["exp_id"] = exp_id
                        row = run_cell(
                            exp_id=exp_id,
                            mapping=mapping,
                            k=k,
                            sf=sf,
                            policy=policy,
                            state=state,
                            tag=args.tag,
                            workload=workload,
                            sanity=sanity,
                            cache_lines=cache_lines,
                            index_hash=args.index_hash,
                            timeout=args.timeout,
                            dry_run=args.dry_run,
                            max_clock=args.max_clock,
                        )
                        rows.append(row)
                        if args.dry_run:
                            print(f"\n[{exp_id}]")
                            print_sanity_report(sanity)
                        else:
                            # Flush after every cell so a long sweep that dies
                            # half way still leaves a usable summary.
                            write_results(rows)

    total = write_results(rows)
    print(f"\nWrote {len(rows)} rows; summary now has {total} at {SUMMARY_CSV}")
    if args.dry_run:
        print("dry run: simulator not invoked")
        return 0
    failed_audit = [
        str(row["exp_id"]) for row in rows if row.get("audit_ok") == "false"
    ]
    if failed_audit:
        print(
            f"AUDIT FAILED for {len(failed_audit)} cell(s): "
            + ", ".join(failed_audit),
            file=sys.stderr,
        )
        return 1
    expected_total = sum(int(row["total_ops_expected"]) for row in rows)
    print(f"audit: all {len(rows)} cell(s) processed "
          f"{expected_total:,} ops with unexpected_pkt=0")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
