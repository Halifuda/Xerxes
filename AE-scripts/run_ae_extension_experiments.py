#!/usr/bin/env python3
"""Run AE extension experiments with explicit trace workloads and mapping sanity.

This script is intentionally self-contained. By default it writes only under
`results/ae_extension/` (override with `--out-root`) and does not read legacy
result CSVs as input.
"""

from __future__ import annotations

import argparse
import csv
import json
import random
import re
import subprocess
import time
from dataclasses import dataclass, replace
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]  # repo root (this script lives in AE-scripts/)
XERXES_BIN = ROOT / "build" / "Xerxes"
DEFAULT_OUT_ROOT = Path("results") / "ae_extension"
RESULTS_DIR = ROOT / DEFAULT_OUT_ROOT
CONFIG_DIR = RESULTS_DIR / "configs"
TRACE_DIR = RESULTS_DIR / "traces"
SANITY_DIR = RESULTS_DIR / "sanity"
DRAMSIM_OUTPUT_DIR = RESULTS_DIR / "dramsim_output"
SUMMARY_CSV = RESULTS_DIR / "ae_extension_results.csv"

TOTAL_HOSTS = 16
TOTAL_MEMS = 16
LEAFS = 4
SPINES = 2
HOSTS_PER_LEAF = 4
MEMS_PER_LEAF = 4
BLOCK_SIZE = 64
GLOBAL_HPA_SIZE = 1 << 20
LEAF_HPA_SIZE = GLOBAL_HPA_SIZE // LEAFS
NO_INTERLEAVE_SEGMENT = GLOBAL_HPA_SIZE // TOTAL_MEMS
DEFAULT_MEM_CAPACITY = 1 << 30
DEFAULT_MEM_DELAY = 40
CACHE_DELAY = 2

SECTIONS = ["1.1", "1.2", "1.3", "2.1", "2.2", "2.3"]
POLICIES = ["LIFO", "FS+LIFO"]
PRIVATE_WINDOW_BYTES = 64 * 1024
SEED_BASE = 10_000
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


@dataclass(frozen=True)
class MemoryEndpoint:
    index: int
    name: str
    leaf: int


@dataclass(frozen=True)
class FlatLayout:
    name: str
    granularity: int
    mem_indices: tuple[int, ...]


@dataclass(frozen=True)
class GroupSpec:
    mem_indices: tuple[int, ...]
    granularity: int


@dataclass(frozen=True)
class GroupedLayout:
    name: str
    group_granularity: int
    groups: tuple[GroupSpec, ...]


@dataclass(frozen=True)
class RunProfile:
    name: str
    noncoh_ops: int
    coh_ops: int
    max_clock: int
    host_cache_lines: int
    sf_entries_21: int
    capacities_22: tuple[int, ...]
    part2_layout_22: str
    part2_layout_23: str
    sf_entries_23: int


MEMORIES = tuple(
    MemoryEndpoint(index=i, name=f"Mem-{i}", leaf=i // MEMS_PER_LEAF)
    for i in range(TOTAL_MEMS)
)

LAYOUTS: dict[str, FlatLayout | GroupedLayout] = {
    "no_interleave": FlatLayout(
        name="no_interleave",
        granularity=NO_INTERLEAVE_SEGMENT,
        mem_indices=tuple(range(TOTAL_MEMS)),
    ),
    "global_16way": FlatLayout(
        name="global_16way",
        granularity=BLOCK_SIZE,
        mem_indices=tuple(range(TOTAL_MEMS)),
    ),
    "leaf_4way": GroupedLayout(
        name="leaf_4way",
        group_granularity=LEAF_HPA_SIZE,
        groups=tuple(
            GroupSpec(
                mem_indices=tuple(range(leaf * MEMS_PER_LEAF, (leaf + 1) * MEMS_PER_LEAF)),
                granularity=BLOCK_SIZE,
            )
            for leaf in range(LEAFS)
        ),
    ),
    "local_2way": GroupedLayout(
        name="local_2way",
        group_granularity=LEAF_HPA_SIZE // 2,
        groups=tuple(
            GroupSpec(mem_indices=(leaf * MEMS_PER_LEAF, leaf * MEMS_PER_LEAF + 1), granularity=BLOCK_SIZE)
            if sub == 0
            else GroupSpec(mem_indices=(leaf * MEMS_PER_LEAF + 2, leaf * MEMS_PER_LEAF + 3), granularity=BLOCK_SIZE)
            for leaf in range(LEAFS)
            for sub in range(2)
        ),
    ),
    "crossleaf_8way": GroupedLayout(
        name="crossleaf_8way",
        group_granularity=GLOBAL_HPA_SIZE // 2,
        groups=(
            GroupSpec(mem_indices=tuple(range(0, 8)), granularity=BLOCK_SIZE),
            GroupSpec(mem_indices=tuple(range(8, 16)), granularity=BLOCK_SIZE),
        ),
    ),
}

PROFILES: dict[str, RunProfile] = {
    "smoke": RunProfile(
        name="smoke",
        noncoh_ops=1500,
        coh_ops=2500,
        max_clock=3_000_000,
        host_cache_lines=64,
        sf_entries_21=128,
        capacities_22=(64, 96, 128, 160, 256),
        part2_layout_22="leaf_4way",
        part2_layout_23="leaf_4way",
        sf_entries_23=128,
    ),
    "full": RunProfile(
        name="full",
        noncoh_ops=6000,
        coh_ops=9000,
        max_clock=8_000_000,
        host_cache_lines=64,
        sf_entries_21=128,
        capacities_22=(64, 96, 128, 160, 256, 512),
        part2_layout_22="leaf_4way",
        part2_layout_23="leaf_4way",
        sf_entries_23=128,
    ),
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run AE extension experiments")
    parser.add_argument(
        "--sections",
        nargs="+",
        choices=SECTIONS + ["all"],
        default=["all"],
    )
    parser.add_argument(
        "--profiles",
        nargs="+",
        choices=list(PROFILES),
        default=["smoke", "full"],
        help="Run one or more profiles in order.",
    )
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument(
        "--out-root",
        default=str(DEFAULT_OUT_ROOT),
        help="Directory for all outputs, relative to the repo root or absolute "
        "(default: results/ae_extension).",
    )
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument(
        "--fresh-summary",
        action="store_true",
        help="Rewrite the summary CSV from this invocation only.",
    )
    parser.add_argument(
        "--clean-evict",
        action="store_true",
        help="Emit clean_evict_notify = true from every host (MemClnEvct on).",
    )
    parser.add_argument(
        "--snoop-entries",
        type=int,
        default=None,
        help="Override the snoop-filter (sigma) capacity for sections 2.1/2.2/2.3. "
        "Section 2.2 iterates capacities_22, so this collapses it to a single rung.",
    )
    parser.add_argument(
        "--policies",
        default=None,
        help="Comma-separated snoop eviction policies (e.g. FIFO,LRU,LFI,LIFO,FS+LIFO); "
        "overrides the module POLICIES when given.",
    )
    parser.add_argument(
        "--private-window-bytes",
        type=int,
        default=None,
        help="Override the per-leaf private window used as 64 KiB by sections 2.1/2.2/2.3.",
    )
    parser.add_argument(
        "--seed-base",
        type=int,
        default=10_000,
        help="Base for Host-*:random_seed (= seed_base + host_idx; default 10000).",
    )
    return parser.parse_args()


def selected_sections(args: argparse.Namespace) -> list[str]:
    if "all" in args.sections:
        return list(SECTIONS)
    return list(args.sections)


def configure_output_root(out_root: str) -> None:
    """Redirect every output path to `out_root` (default keeps legacy paths)."""
    global RESULTS_DIR, CONFIG_DIR, TRACE_DIR, SANITY_DIR, DRAMSIM_OUTPUT_DIR, SUMMARY_CSV
    base = Path(out_root)
    if not base.is_absolute():
        base = ROOT / base
    RESULTS_DIR = base
    CONFIG_DIR = base / "configs"
    TRACE_DIR = base / "traces"
    SANITY_DIR = base / "sanity"
    DRAMSIM_OUTPUT_DIR = base / "dramsim_output"
    SUMMARY_CSV = base / "ae_extension_results.csv"


def ensure_dirs() -> None:
    for path in [RESULTS_DIR, CONFIG_DIR, TRACE_DIR, SANITY_DIR, DRAMSIM_OUTPUT_DIR]:
        path.mkdir(parents=True, exist_ok=True)


def leaf_of_host(host_idx: int) -> int:
    return host_idx // HOSTS_PER_LEAF


def leaf_base(leaf: int) -> int:
    return leaf * LEAF_HPA_SIZE


def mem_name(mem_idx: int) -> str:
    return MEMORIES[mem_idx].name


def make_host(
    name: str,
    coherent: bool,
    random_seed: int,
    trace_file: str,
    cache_capacity: int,
    wr_ratio: float,
    clean_evict: bool,
) -> str:
    return f"""[{name}]
q_capacity = 32
cache_capacity = {cache_capacity}
cache_delay = {CACHE_DELAY}
issue_delay = 0
coherent = {str(coherent).lower()}
burst_size = 1
block_size = {BLOCK_SIZE}
interleave_type = "trace"
interleave_param = 0
hot_req_ratio = 1.0
hot_region_ratio = 1.0
trace_file = "{trace_file}"
random_seed = {random_seed}
hpa_base = 0
hpa_size = {GLOBAL_HPA_SIZE}
wr_ratio = {wr_ratio}
clean_evict_notify = {str(clean_evict).lower()}"""


def make_mem(name: str) -> str:
    return f"""[{name}]
tick_per_clock = 1
process_time = {DEFAULT_MEM_DELAY}
start = 0
capacity = {DEFAULT_MEM_CAPACITY}
wr_ratio = 0.0
config_file = "DRAMsim3/configs/DDR4_8Gb_x8_3200.ini"
output_dir = "{DRAMSIM_OUTPUT_DIR}" """


def make_switch(name: str, delay: int) -> str:
    return f"""[{name}]
delay = {delay}"""


def make_snoop(name: str, entries: int, eviction: str) -> str:
    return f"""[{name}]
line_num = {entries}
assoc = 4
max_burst_inv = 8
ranges = [[0, {GLOBAL_HPA_SIZE}]]
eviction = "{eviction}" """


def toml_edges(edge_list: list[tuple[str, str]]) -> str:
    lines = ["["]
    for src, dst in edge_list:
        lines.append(f'    ["{src}", "{dst}"],')
    lines.append("]")
    return "\n".join(lines)


def layout_toml(layout_name: str) -> str:
    layout = LAYOUTS[layout_name]
    if isinstance(layout, FlatLayout):
        mems = ", ".join(f'"{mem_name(idx)}"' for idx in layout.mem_indices)
        return f"""[address_system]
hpa_start = 0
hpa_size = {GLOBAL_HPA_SIZE}
ways = {len(layout.mem_indices)}
granularity = {layout.granularity}
memories = [{mems}]"""

    lines = [
        "[address_system]",
        "grouped = true",
        "hpa_start = 0",
        f"hpa_size = {GLOBAL_HPA_SIZE}",
        f"group_granularity = {layout.group_granularity}",
    ]
    for group in layout.groups:
        mems = ", ".join(f'"{mem_name(idx)}"' for idx in group.mem_indices)
        lines.extend(
            [
                "",
                "[[address_system.groups]]",
                f"ways = {len(group.mem_indices)}",
                f"granularity = {group.granularity}",
                f"memories = [{mems}]",
            ]
        )
    return "\n".join(lines)


def build_spineleaf_config(
    *,
    exp_id: str,
    layout_name: str,
    coherent: bool,
    trace_files: list[Path],
    cache_capacity: int,
    wr_ratio: float,
    snoop_policy: str | None,
    snoop_entries: int | None,
    max_clock: int,
    clean_evict: bool,
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

    for host_idx in range(TOTAL_HOSTS):
        name = f"Host-{host_idx}"
        devices.append(f'{name} = "Requester"')
        configs.append(
            make_host(
                name=name,
                coherent=coherent,
                random_seed=SEED_BASE + host_idx,
                trace_file=str(trace_files[host_idx]),
                cache_capacity=cache_capacity,
                wr_ratio=wr_ratio,
                clean_evict=clean_evict,
            )
        )
        edges.append((name, f"Leaf-{leaf_of_host(host_idx)}"))

    for mem in MEMORIES:
        devices.append(f'{mem.name} = "DRAMsim3Interface"')
        configs.append(make_mem(mem.name))
        if coherent and snoop_policy and snoop_entries is not None:
            snoop_name = f"Snoop-{mem.index}"
            devices.append(f'{snoop_name} = "Snoop"')
            configs.append(make_snoop(snoop_name, snoop_entries, snoop_policy))
            edges.append((f"Leaf-{mem.leaf}", snoop_name))
            edges.append((snoop_name, mem.name))
        else:
            edges.append((f"Leaf-{mem.leaf}", mem.name))

    for leaf in range(LEAFS):
        for spine in range(SPINES):
            edges.append((f"Leaf-{leaf}", f"Spine-{spine}"))

    cfg_text = TOML_HEADER.format(
        max_clock=max_clock,
        log_name=str(RESULTS_DIR / f"{exp_id}.csv"),
        edges=toml_edges(edges),
        devices="\n".join(devices),
        device_configs="\n".join(configs) + "\n\n" + layout_toml(layout_name),
    )
    cfg_path = CONFIG_DIR / f"{exp_id}.toml"
    cfg_path.write_text(cfg_text, encoding="utf-8")
    return cfg_path


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


def add_derived_metrics(row: dict[str, object]) -> None:
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
        row["host_sum_bw_gbps"] = sum(float(row[col]) for col in host_metric_cols["bw_gbps"] if row.get(col) != "")
    if "avg_latency_ns" in host_metric_cols:
        if req_cols:
            weighted_numer = 0.0
            weighted_denom = 0.0
            req_order = sorted(req_cols)
            lat_order = sorted(host_metric_cols["avg_latency_ns"])
            for req_col, lat_col in zip(req_order, lat_order):
                req = float(row.get(req_col, 0.0) or 0.0)
                lat = float(row.get(lat_col, 0.0) or 0.0)
                weighted_numer += req * lat
                weighted_denom += req
            row["host_weighted_avg_latency_ns"] = weighted_numer / weighted_denom if weighted_denom else ""
        else:
            lats = [float(row[col]) for col in host_metric_cols["avg_latency_ns"] if row.get(col) != ""]
            row["host_weighted_avg_latency_ns"] = sum(lats) / len(lats) if lats else ""
    if "avg_evict_wait_ns" in host_metric_cols:
        waits = [float(row[col]) for col in host_metric_cols["avg_evict_wait_ns"] if row.get(col) != ""]
        row["host_mean_avg_evict_wait_ns"] = sum(waits) / len(waits) if waits else ""

    for metric, cols in snoop_metric_cols.items():
        values = [float(row[col]) for col in cols if row.get(col) != ""]
        if not values:
            continue
        if metric.startswith("avg_"):
            row[f"snoop_mean_{metric}"] = sum(values) / len(values)
        else:
            row[f"snoop_sum_{metric}"] = sum(values)


def run_xerxes(cfg_path: Path, timeout: int) -> tuple[bool, str]:
    result = subprocess.run(
        [str(XERXES_BIN), str(cfg_path)],
        capture_output=True,
        text=True,
        timeout=timeout,
        cwd=ROOT,
    )
    ok = result.returncode == 0
    return ok, (result.stdout + "\n" + result.stderr).strip()


def layout_query(layout_name: str, hpa: int) -> int:
    layout = LAYOUTS[layout_name]
    if isinstance(layout, FlatLayout):
        offset = hpa
        stripe = offset // layout.granularity
        target_idx = stripe % len(layout.mem_indices)
        return layout.mem_indices[target_idx]

    offset = hpa
    group_stripe = offset // layout.group_granularity
    group_idx = group_stripe % len(layout.groups)
    group = layout.groups[group_idx]
    full_round_bytes = (group_stripe // len(layout.groups)) * layout.group_granularity
    inner_offset = full_round_bytes + (offset % layout.group_granularity)
    inner_stripe = inner_offset // group.granularity
    target_idx = inner_stripe % len(group.mem_indices)
    return group.mem_indices[target_idx]


def read_trace_ops(trace_path: Path) -> list[dict[str, int | str]]:
    ops: list[dict[str, int | str]] = []
    with trace_path.open() as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            addr_hex, op_type, tick_str = line.split()
            ops.append(
                {
                    "addr": int(addr_hex, 16),
                    "type": op_type,
                    "tick": int(tick_str),
                }
            )
    return ops


def sanity_for_layout(layout_name: str, trace_files: list[Path]) -> dict[str, object]:
    host_summaries: list[dict[str, object]] = []
    local_fracs: list[float] = []
    remote_fracs: list[float] = []
    touched_counts: list[int] = []
    per_mem_totals = {mem.name: 0 for mem in MEMORIES}

    for host_idx, trace_path in enumerate(trace_files):
        ops = read_trace_ops(trace_path)
        counts: dict[int, int] = {}
        local_ops = 0
        for entry in ops:
            mem_idx = layout_query(layout_name, int(entry["addr"]))
            counts[mem_idx] = counts.get(mem_idx, 0) + 1
            per_mem_totals[mem_name(mem_idx)] += 1
            if MEMORIES[mem_idx].leaf == leaf_of_host(host_idx):
                local_ops += 1
        total_ops = len(ops)
        local_frac = local_ops / total_ops if total_ops else 0.0
        remote_frac = 1.0 - local_frac if total_ops else 0.0
        touched = sorted(counts)
        local_fracs.append(local_frac)
        remote_fracs.append(remote_frac)
        touched_counts.append(len(touched))
        host_summaries.append(
            {
                "host": f"Host-{host_idx}",
                "leaf": leaf_of_host(host_idx),
                "trace_ops": total_ops,
                "effective_local_fraction": local_frac,
                "effective_remote_fraction": remote_frac,
                "touched_memory_endpoints": [mem_name(idx) for idx in touched],
                "per_memory_ops": {mem_name(idx): counts[idx] for idx in touched},
            }
        )

    summary = {
        "layout": layout_name,
        "host_count": len(host_summaries),
        "mean_local_fraction": sum(local_fracs) / len(local_fracs),
        "min_local_fraction": min(local_fracs),
        "max_local_fraction": max(local_fracs),
        "mean_remote_fraction": sum(remote_fracs) / len(remote_fracs),
        "mean_touched_endpoints": sum(touched_counts) / len(touched_counts),
        "min_touched_endpoints": min(touched_counts),
        "max_touched_endpoints": max(touched_counts),
        "global_touched_endpoints": sum(1 for value in per_mem_totals.values() if value > 0),
        "per_memory_total_ops": per_mem_totals,
        "hosts": host_summaries,
    }
    return summary


def sanity_columns(sanity: dict[str, object]) -> dict[str, object]:
    hosts = sanity["hosts"]
    host_fragments = []
    touched_fragments = []
    for host in hosts:
        host_fragments.append(f"{host['host']}={host['effective_local_fraction']:.3f}")
        touched_fragments.append(
            f"{host['host']}={','.join(host['touched_memory_endpoints'])}"
        )
    return {
        "sanity_mean_local_fraction": f"{sanity['mean_local_fraction']:.6f}",
        "sanity_mean_remote_fraction": f"{sanity['mean_remote_fraction']:.6f}",
        "sanity_min_local_fraction": f"{sanity['min_local_fraction']:.6f}",
        "sanity_max_local_fraction": f"{sanity['max_local_fraction']:.6f}",
        "sanity_mean_touched_endpoints": f"{sanity['mean_touched_endpoints']:.3f}",
        "sanity_global_touched_endpoints": int(sanity["global_touched_endpoints"]),
        "sanity_host_local_fractions": ";".join(host_fragments),
        "sanity_host_touched_endpoints": ";".join(touched_fragments),
    }


def write_trace(path: Path, ops: list[tuple[int, str, int]]) -> None:
    path.write_text("\n".join(f"{addr:x} {op} {tick}" for addr, op, tick in ops), encoding="utf-8")


def random_window_ops(
    *,
    seed: int,
    base: int,
    size: int,
    count: int,
    op_type: str,
    start_tick: int = 0,
    tick_stride: int = 1,
    wrap_size: int | None = None,
    wrap_base: int = 0,
) -> list[tuple[int, str, int]]:
    rng = random.Random(seed)
    blocks = max(1, size // BLOCK_SIZE)
    ops: list[tuple[int, str, int]] = []
    tick = start_tick
    for _ in range(count):
        addr = base + rng.randrange(blocks) * BLOCK_SIZE
        if wrap_size is not None:
            addr = wrap_base + ((addr - wrap_base) % wrap_size)
        ops.append((addr, op_type, tick))
        tick += tick_stride
    return ops


def streaming_unique_ops(
    *,
    base: int,
    line_count: int,
    op_type: str,
    start_tick: int,
    tick_stride: int = 1,
    count: int | None = None,
) -> list[tuple[int, str, int]]:
    """Stream over `line_count` consecutive lines, wrapping when `count` exceeds it.

    With `count=None` the historical behaviour is kept: exactly `line_count`
    operations, one per line. Otherwise `count` operations are emitted and the
    address index cycles through `line_idx % line_count`, so the footprint stays
    bounded by `line_count` lines while the op count is preserved.
    """
    if line_count <= 0:
        raise ValueError("line_count must be positive")
    op_count = line_count if count is None else count
    ops: list[tuple[int, str, int]] = []
    tick = start_tick
    for op_idx in range(op_count):
        ops.append((base + (op_idx % line_count) * BLOCK_SIZE, op_type, tick))
        tick += tick_stride
    return ops


def generate_noncoherent_traces(
    *,
    exp_id: str,
    variant: str,
    ops_per_host: int,
    footprint_bytes: int,
    tick_stride: int = 1,
) -> tuple[list[Path], dict[str, object]]:
    trace_files: list[Path] = []
    metadata: dict[str, object] = {
        "trace_variant": variant,
        "footprint_bytes": footprint_bytes,
        "ops_per_host": ops_per_host,
        "tick_stride": tick_stride,
    }
    for host_idx in range(TOTAL_HOSTS):
        leaf = leaf_of_host(host_idx)
        base = leaf_base(leaf)
        if variant == "perfect_locality":
            window_base = base
            wrap_size = None
            wrap_base = 0
        elif variant == "footprint_growth":
            # 1.2 starts from each host's own local memory segment. At small
            # footprints no_interleave maps each host to one local endpoint;
            # as the footprint grows up to one leaf group, the window wraps
            # inside that leaf so all hosts still represent local-group data.
            # Larger footprints spill into the global pool.
            local_mem = leaf * MEMS_PER_LEAF + (host_idx % HOSTS_PER_LEAF)
            window_base = local_mem * NO_INTERLEAVE_SEGMENT
            if footprint_bytes <= LEAF_HPA_SIZE:
                wrap_base = base
                wrap_size = LEAF_HPA_SIZE
            else:
                wrap_base = 0
                wrap_size = GLOBAL_HPA_SIZE
        else:
            raise ValueError(f"unknown non-coherent variant {variant}")
        trace_path = TRACE_DIR / f"{exp_id}_host{host_idx}.trace"
        ops = random_window_ops(
            seed=20_000 + host_idx,
            base=window_base,
            size=footprint_bytes,
            count=ops_per_host,
            op_type="R",
            tick_stride=tick_stride,
            wrap_size=wrap_size,
            wrap_base=wrap_base,
        )
        write_trace(trace_path, ops)
        trace_files.append(trace_path)
    return trace_files, metadata


def generate_coherent_traces(
    *,
    exp_id: str,
    ops_per_host: int,
    shared_ratio: float,
    sharing_shape: str,
    private_window_bytes: int,
    shared_window_bytes: int,
    private_phase_ratio: float = 0.5,
    shared_op_type: str = "R",
    tick_stride: int = 1,
    shared_revisit_rounds: int = 1,
) -> tuple[list[Path], dict[str, object]]:
    trace_files: list[Path] = []
    shared_ops = int(ops_per_host * shared_ratio)
    private_ops = ops_per_host - shared_ops
    private_pre = int(private_ops * private_phase_ratio)
    private_post = private_ops - private_pre
    metadata: dict[str, object] = {
        "ops_per_host": ops_per_host,
        "shared_ratio": shared_ratio,
        "sharing_shape": sharing_shape,
        "private_window_bytes": private_window_bytes,
        "shared_window_bytes": shared_window_bytes,
        "private_pre_ops": private_pre,
        "private_post_ops": private_post,
        "shared_ops": shared_ops,
        "shared_op_type": shared_op_type,
        "tick_stride": tick_stride,
        "shared_revisit_rounds": shared_revisit_rounds,
    }

    private_slot = private_window_bytes // HOSTS_PER_LEAF
    if private_slot < BLOCK_SIZE:
        raise ValueError("private window is too small")
    # Each host owns one private_slot-sized, leaf-local window. Split that window
    # into a pre and a post phase so the two phases never share lines, and keep
    # the op counts (not the unique line count) at the requested totals by
    # wrapping inside each phase.
    lines_per_host = (private_window_bytes // HOSTS_PER_LEAF) // BLOCK_SIZE
    pre_lines = max(1, int(lines_per_host * private_phase_ratio))
    post_lines = max(1, lines_per_host - pre_lines)

    for host_idx in range(TOTAL_HOSTS):
        leaf = leaf_of_host(host_idx)
        local_leaf_base = leaf_base(leaf)
        shared_base = 0 if sharing_shape == "global" else local_leaf_base
        private_base = local_leaf_base + shared_window_bytes + (host_idx % HOSTS_PER_LEAF) * private_slot
        private_post_base = private_base + pre_lines * BLOCK_SIZE

        ops: list[tuple[int, str, int]] = []
        ops.extend(
            streaming_unique_ops(
                base=private_base,
                line_count=pre_lines,
                count=private_pre,
                op_type="W",
                start_tick=0,
                tick_stride=tick_stride,
            )
        )
        if shared_ops:
            rounds = max(1, shared_revisit_rounds)
            per_round = max(1, shared_ops // rounds)
            for round_idx in range(rounds):
                ops.extend(
                    random_window_ops(
                        seed=30_000 + host_idx + round_idx * 1000,
                        base=shared_base,
                        size=shared_window_bytes,
                        count=per_round if round_idx + 1 < rounds else shared_ops - per_round * (rounds - 1),
                        op_type=shared_op_type,
                        start_tick=(ops[-1][2] + tick_stride) if ops else 0,
                        tick_stride=tick_stride,
                    )
                )
        ops.extend(
            streaming_unique_ops(
                base=private_post_base,
                line_count=post_lines,
                count=private_post,
                op_type="W",
                start_tick=(ops[-1][2] + tick_stride) if ops else 0,
                tick_stride=tick_stride,
            )
        )
        trace_path = TRACE_DIR / f"{exp_id}_host{host_idx}.trace"
        write_trace(trace_path, ops)
        trace_files.append(trace_path)
    return trace_files, metadata


def run_experiment(
    *,
    row: dict[str, object],
    trace_files: list[Path],
    profile: RunProfile,
    timeout: int,
    dry_run: bool,
    clean_evict: bool,
) -> dict[str, object]:
    exp_id = str(row["exp_id"])
    cfg_path = build_spineleaf_config(
        exp_id=exp_id,
        layout_name=str(row["layout"]),
        coherent=bool(row["coherent_bool"]),
        trace_files=trace_files,
        cache_capacity=profile.host_cache_lines,
        wr_ratio=float(row["wr_ratio"]),
        snoop_policy=(str(row["policy"]) if row["policy"] else None),
        snoop_entries=(int(row["snoop_entries"]) if row["snoop_entries"] else None),
        max_clock=profile.max_clock,
        clean_evict=clean_evict,
    )
    log_path = RESULTS_DIR / f"{exp_id}.csv"
    sanity = sanity_for_layout(str(row["layout"]), trace_files)
    sanity_path = SANITY_DIR / f"{exp_id}.json"
    sanity_payload = {
        "exp_id": exp_id,
        "row": {k: v for k, v in row.items() if k != "coherent_bool"},
        "sanity": sanity,
    }
    sanity_path.write_text(json.dumps(sanity_payload, indent=2), encoding="utf-8")
    row.update(sanity_columns(sanity))
    row["sanity_json"] = str(sanity_path.relative_to(ROOT))
    row["config_toml"] = str(cfg_path.relative_to(ROOT))
    row["trace_prefix"] = str((TRACE_DIR / f"{exp_id}_host*.trace").relative_to(ROOT))
    row["clean_evict_notify"] = "true" if clean_evict else "false"

    if dry_run:
        row["run_status"] = "dry_run"
        return row

    print(f"[{exp_id}] ", end="", flush=True)
    start = time.time()
    ok, output = run_xerxes(cfg_path, timeout)
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
    return row


def load_existing_results() -> list[dict[str, object]]:
    if not SUMMARY_CSV.exists():
        return []
    with SUMMARY_CSV.open(newline="") as fh:
        return list(csv.DictReader(fh))


def write_results(
    rows: list[dict[str, object]],
    *,
    merge_existing: bool = True,
    replace_scope: set[tuple[str, str]] | None = None,
) -> int:
    if merge_existing:
        merged: dict[str, dict[str, object]] = {}
        for row in load_existing_results():
            if replace_scope and (str(row.get("profile", "")), str(row.get("section", ""))) in replace_scope:
                continue
            exp_id = str(row.get("exp_id", ""))
            if exp_id:
                merged[exp_id] = row
        for row in rows:
            exp_id = str(row.get("exp_id", ""))
            if exp_id:
                merged[exp_id] = row
        rows_to_write = list(merged.values())
    else:
        rows_to_write = rows

    ordered_meta = [
        "profile",
        "section",
        "experiment",
        "exp_id",
        "layout",
        "workload",
        "coherent",
        "policy",
        "snoop_entries",
        "footprint_bytes",
        "shared_ratio",
        "sharing_shape",
        "private_window_bytes",
        "shared_window_bytes",
        "cache_capacity_lines",
        "trace_ops_per_host",
        "run_status",
        "elapsed_sec",
        "sanity_mean_local_fraction",
        "sanity_mean_remote_fraction",
        "sanity_min_local_fraction",
        "sanity_max_local_fraction",
        "sanity_mean_touched_endpoints",
        "sanity_global_touched_endpoints",
        "sanity_host_local_fractions",
        "sanity_host_touched_endpoints",
        "sanity_json",
        "config_toml",
        "trace_prefix",
        "notes",
        "failure_excerpt",
    ]
    keys = sorted({key for row in rows_to_write for key in row.keys() if key not in ordered_meta and key != "coherent_bool"})
    with SUMMARY_CSV.open("w", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=ordered_meta + keys)
        writer.writeheader()
        for row in rows_to_write:
            writer.writerow({key: row.get(key, "") for key in ordered_meta + keys})
    return len(rows_to_write)


def base_row(
    *,
    profile: RunProfile,
    section: str,
    experiment: str,
    exp_id: str,
    layout: str,
    workload: str,
    coherent: bool,
    policy: str = "",
    snoop_entries: int | None = None,
) -> dict[str, object]:
    return {
        "profile": profile.name,
        "section": section,
        "experiment": experiment,
        "exp_id": exp_id,
        "layout": layout,
        "workload": workload,
        "coherent": "true" if coherent else "false",
        "coherent_bool": coherent,
        "policy": policy,
        "snoop_entries": snoop_entries if snoop_entries is not None else "",
        "cache_capacity_lines": profile.host_cache_lines,
        "wr_ratio": 0.0,
        "notes": "",
    }


def section_11(
    profile: RunProfile, timeout: int, dry_run: bool, clean_evict: bool
) -> list[dict[str, object]]:
    rows = []
    footprint = 32 * 1024
    for layout in ["no_interleave", "leaf_4way", "global_16way"]:
        exp_id = f"v2_{profile.name}_11_{layout}"
        trace_files, trace_meta = generate_noncoherent_traces(
            exp_id=exp_id,
            variant="perfect_locality",
            ops_per_host=profile.noncoh_ops,
            footprint_bytes=footprint,
        )
        row = base_row(
            profile=profile,
            section="1.1",
            experiment="perfect_locality",
            exp_id=exp_id,
            layout=layout,
            workload="trace_perfect_locality",
            coherent=False,
        )
        row["footprint_bytes"] = footprint
        row["trace_ops_per_host"] = trace_meta["ops_per_host"]
        row["notes"] = "Leaf-local shared hot window per leaf; cache intentionally small to force misses."
        rows.append(
                run_experiment(
                    row=row,
                    trace_files=trace_files,
                    profile=profile,
                    timeout=timeout,
                    dry_run=dry_run,
                    clean_evict=clean_evict,
                )
            )
    return rows


def section_12(
    profile: RunProfile, timeout: int, dry_run: bool, clean_evict: bool
) -> list[dict[str, object]]:
    rows = []
    footprints = [16 * 1024, 128 * 1024, 256 * 1024, GLOBAL_HPA_SIZE]
    if profile.name == "full":
        footprints = [16 * 1024, 64 * 1024, 128 * 1024, 256 * 1024, GLOBAL_HPA_SIZE]
    for footprint in footprints:
        for layout in ["no_interleave", "leaf_4way", "global_16way"]:
            exp_id = f"v2_{profile.name}_12_{layout}_{footprint}"
            trace_files, trace_meta = generate_noncoherent_traces(
                exp_id=exp_id,
                variant="footprint_growth",
                ops_per_host=profile.noncoh_ops,
                footprint_bytes=footprint,
                tick_stride=30,
            )
            row = base_row(
                profile=profile,
                section="1.2",
                experiment="hot_footprint_growth",
                exp_id=exp_id,
                layout=layout,
                workload="trace_footprint_growth",
                coherent=False,
            )
            row["footprint_bytes"] = footprint
            row["trace_ops_per_host"] = trace_meta["ops_per_host"]
            rows.append(
                run_experiment(
                    row=row,
                    trace_files=trace_files,
                    profile=profile,
                    timeout=timeout,
                    dry_run=dry_run,
                    clean_evict=clean_evict,
                )
            )
    return rows


def section_13(
    profile: RunProfile, timeout: int, dry_run: bool, clean_evict: bool
) -> list[dict[str, object]]:
    rows = []
    footprint = 32 * 1024
    for layout in ["local_2way", "leaf_4way", "crossleaf_8way"]:
        exp_id = f"v2_{profile.name}_13_{layout}"
        trace_files, trace_meta = generate_noncoherent_traces(
            exp_id=exp_id,
            variant="perfect_locality",
            ops_per_host=profile.noncoh_ops,
            footprint_bytes=footprint,
        )
        row = base_row(
            profile=profile,
            section="1.3",
            experiment="group_size_sensitivity",
            exp_id=exp_id,
            layout=layout,
            workload="trace_perfect_locality",
            coherent=False,
        )
        row["footprint_bytes"] = footprint
        row["trace_ops_per_host"] = trace_meta["ops_per_host"]
        rows.append(
                run_experiment(
                    row=row,
                    trace_files=trace_files,
                    profile=profile,
                    timeout=timeout,
                    dry_run=dry_run,
                    clean_evict=clean_evict,
                )
            )
    return rows


def section_21(
    profile: RunProfile, timeout: int, dry_run: bool, clean_evict: bool
) -> list[dict[str, object]]:
    rows = []
    for layout in ["no_interleave", "leaf_4way", "global_16way"]:
        for policy in POLICIES:
            exp_id = f"v2_{profile.name}_21_{layout}_{policy.lower().replace('+', 'p')}"
            trace_files, trace_meta = generate_coherent_traces(
                exp_id=exp_id,
                ops_per_host=profile.coh_ops,
                shared_ratio=0.5,
                sharing_shape="leaf",
                private_window_bytes=PRIVATE_WINDOW_BYTES,
                shared_window_bytes=16 * 1024,
                private_phase_ratio=0.1,
            )
            row = base_row(
                profile=profile,
                section="2.1",
                experiment="placement_induced_sf_pressure",
                exp_id=exp_id,
                layout=layout,
                workload="trace_private_shared_leaf",
                coherent=True,
                policy=policy,
                snoop_entries=profile.sf_entries_21,
            )
            row["trace_ops_per_host"] = trace_meta["ops_per_host"]
            row["shared_ratio"] = trace_meta["shared_ratio"]
            row["sharing_shape"] = trace_meta["sharing_shape"]
            row["private_window_bytes"] = trace_meta["private_window_bytes"]
            row["shared_window_bytes"] = trace_meta["shared_window_bytes"]
            rows.append(
                run_experiment(
                    row=row,
                    trace_files=trace_files,
                    profile=profile,
                    timeout=timeout,
                    dry_run=dry_run,
                    clean_evict=clean_evict,
                )
            )
    return rows


def section_22(
    profile: RunProfile, timeout: int, dry_run: bool, clean_evict: bool
) -> list[dict[str, object]]:
    rows = []
    for entries in profile.capacities_22:
        for policy in POLICIES:
            exp_id = f"v2_{profile.name}_22_{profile.part2_layout_22}_{entries}_{policy.lower().replace('+', 'p')}"
            trace_files, trace_meta = generate_coherent_traces(
                exp_id=exp_id,
                ops_per_host=profile.coh_ops,
                shared_ratio=0.8,
                sharing_shape="leaf",
                private_window_bytes=PRIVATE_WINDOW_BYTES,
                shared_window_bytes=16 * 1024,
                private_phase_ratio=0.1,
            )
            row = base_row(
                profile=profile,
                section="2.2",
                experiment="sf_capacity",
                exp_id=exp_id,
                layout=profile.part2_layout_22,
                workload="trace_private_shared_leaf",
                coherent=True,
                policy=policy,
                snoop_entries=entries,
            )
            row["trace_ops_per_host"] = trace_meta["ops_per_host"]
            row["shared_ratio"] = trace_meta["shared_ratio"]
            row["sharing_shape"] = trace_meta["sharing_shape"]
            row["private_window_bytes"] = trace_meta["private_window_bytes"]
            row["shared_window_bytes"] = trace_meta["shared_window_bytes"]
            rows.append(
                run_experiment(
                    row=row,
                    trace_files=trace_files,
                    profile=profile,
                    timeout=timeout,
                    dry_run=dry_run,
                    clean_evict=clean_evict,
                )
            )
    return rows


def section_23(
    profile: RunProfile, timeout: int, dry_run: bool, clean_evict: bool
) -> list[dict[str, object]]:
    rows = []
    ratios = [0.0, 0.5, 0.8]
    for shape in ["leaf", "global"]:
        for ratio in ratios:
            for policy in POLICIES:
                ratio_slug = str(ratio).replace(".", "p")
                exp_id = f"v2_{profile.name}_23_{profile.part2_layout_23}_{shape}_{ratio_slug}_{policy.lower().replace('+', 'p')}"
                trace_files, trace_meta = generate_coherent_traces(
                    exp_id=exp_id,
                    ops_per_host=profile.coh_ops,
                    shared_ratio=ratio,
                    sharing_shape=shape,
                    private_window_bytes=PRIVATE_WINDOW_BYTES,
                    shared_window_bytes=16 * 1024,
                    private_phase_ratio=0.1,
                )
                row = base_row(
                    profile=profile,
                    section="2.3",
                    experiment="sharing_ratio_shape",
                    exp_id=exp_id,
                    layout=profile.part2_layout_23,
                    workload=f"trace_private_shared_{shape}",
                    coherent=True,
                    policy=policy,
                    snoop_entries=profile.sf_entries_23,
                )
                row["trace_ops_per_host"] = trace_meta["ops_per_host"]
                row["shared_ratio"] = trace_meta["shared_ratio"]
                row["sharing_shape"] = trace_meta["sharing_shape"]
                row["private_window_bytes"] = trace_meta["private_window_bytes"]
                row["shared_window_bytes"] = trace_meta["shared_window_bytes"]
                rows.append(
                run_experiment(
                    row=row,
                    trace_files=trace_files,
                    profile=profile,
                    timeout=timeout,
                    dry_run=dry_run,
                    clean_evict=clean_evict,
                )
            )
    return rows


SECTION_RUNNERS = {
    "1.1": section_11,
    "1.2": section_12,
    "1.3": section_13,
    "2.1": section_21,
    "2.2": section_22,
    "2.3": section_23,
}


def main() -> int:
    global POLICIES, PRIVATE_WINDOW_BYTES, SEED_BASE
    args = parse_args()
    if args.policies is not None:
        parsed = [name.strip() for name in args.policies.split(",") if name.strip()]
        if not parsed:
            raise SystemExit("--policies must name at least one policy")
        POLICIES = parsed
    if args.private_window_bytes is not None:
        PRIVATE_WINDOW_BYTES = args.private_window_bytes
    SEED_BASE = args.seed_base
    configure_output_root(args.out_root)
    ensure_dirs()
    rows: list[dict[str, object]] = []
    chosen_sections = selected_sections(args)
    total_written = 0
    replace_scope = {
        (profile_name, section)
        for profile_name in args.profiles
        for section in chosen_sections
    }

    for profile_name in args.profiles:
        profile = PROFILES[profile_name]
        if args.snoop_entries is not None:
            # One knob for the whole sweep: the section-2.1/2.3 switch (sigma)
            # plus section 2.2's capacity list, which is collapsed to one rung
            # so section_22 still loops exactly once.
            profile = replace(
                profile,
                sf_entries_21=args.snoop_entries,
                capacities_22=(args.snoop_entries,),
                sf_entries_23=args.snoop_entries,
            )
        print(f"\n=== profile: {profile.name} ===")
        for section in chosen_sections:
            print(f"--- section {section} ---")
            rows.extend(SECTION_RUNNERS[section](profile, args.timeout, args.dry_run, args.clean_evict))
            total_written = write_results(
                rows,
                merge_existing=not args.fresh_summary,
                replace_scope=replace_scope,
            )

    print(f"\nWrote {len(rows)} new/updated rows; summary now has {total_written} rows at {SUMMARY_CSV}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
