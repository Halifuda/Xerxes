#!/usr/bin/env python3
"""Pure generator for the revised 5.4 workload ("wl2").

The workload is a *pure function* of its parameters: no engine state, no I/O,
and no imports from the C++ simulator.  The runner (run_wl2.py) owns the files,
the configs and the invocation; this module only produces the address stream
and the address-to-memory mapping that must agree with the engine.

Layout (single [address_system] region, 32 slots of S_BYTES):

    private: host h occupies lines [0, P_LINES) of slot h
             i.e. HPA [h*S_BYTES, h*S_BYTES + P_BYTES)
    shared:  host h owns slot 16 + h
             i.e. HPA [16*S_BYTES + h*S_BYTES, ... + S_BYTES)

    HPA range covered: [0, 32*S_BYTES) = [0, 2,099,200)  (HPA_SIZE = 4 MiB)

Per host (9,000 ops, 144,000 total):

    1. private prefix: the first 10% of the private op budget (450 by default),
       a wraparound sequential scan of the host's P private lines (P=257 by
       default, overridable through generate(private_lines=...)), op=W unless
       private_op_type overrides it
    2. shared block: picks a publisher among the k candidates, then a line
       inside that publisher's 1,025-line instance by Zipf(alpha), op=R
    3. private suffix: the remaining 4,050 ops, continuing the same scan

The wraparound scan is the point of the private phase: 257 lines is far larger
than the 64-line host cache, so the phase is cold and leaves no reuse for the
policy to exploit, while still producing one insert per op.  Shrinking
private_lines below the host cache size instead lets the host absorb the whole
private working set, which is why it is a knob.

line_offset (default 0) shifts every generated address by line_offset 64-byte
lines, private and shared alike.  Because the shift is a whole number of lines,
the snoop-filter index becomes (line + line_offset) % sets: a pure rotation of
which set each line lands in, with the line -> line mapping otherwise untouched.
It must be a multiple of 64 lines, which keeps line % 16 (the 16-way global
endpoint) and line % 64 (the host cache's 64-line index) fixed, so the shift
changes only the SF bucket assignment.
"""

from __future__ import annotations

import bisect
import random
from dataclasses import dataclass
from typing import NamedTuple, Sequence, Union

# ---------------------------------------------------------------------------
# Fixed geometry.  Nothing here is a knob except where a comment says so.
# ---------------------------------------------------------------------------

BLOCK_SIZE = 64

# Instance sizes.  Both line counts are coprime with the interleave widths
# (4 and 16), which is what keeps the per-endpoint footprints balanced.
S_LINES = 1025
S_BYTES = S_LINES * BLOCK_SIZE          # 65,600
P_LINES = 257
P_BYTES = P_LINES * BLOCK_SIZE          # 16,448

# 16 private slots followed by 16 shared slots.
S_SLOTS = 32

# Cluster-strided shared layout (--sharers n): each cluster of n hosts owns a
# slice of CLUSTER_ROWS * n lines, striped over its own n memory endpoints, so
# that every SF tracks the same number of shared lines at every n.
CLUSTER_ROWS = 512
HPA_SIZE = 4 << 20                      # 4 MiB, one region
HPA_SPAN = S_SLOTS * S_BYTES            # 2,099,200 < HPA_SIZE

TOTAL_HOSTS = 16
TOTAL_MEMS = 16
LEAFS = 4
HOSTS_PER_LEAF = 4
MEMS_PER_LEAF = 4

PRIVATE_BASE = 0
PRIVATE_SLOT_BYTES = S_BYTES
SHARED_BASE = TOTAL_HOSTS * S_BYTES     # 1,049,600
SHARED_SLOT_BYTES = S_BYTES

# Per-host op budget.
OPS_PER_HOST = 9_000
TOTAL_OPS = OPS_PER_HOST * TOTAL_HOSTS  # 144,000

# Defaults of the knobs the runner exposes.
DEFAULT_SHARED_RATIO = 0.5
DEFAULT_PRIVATE_PHASE_RATIO = 0.1
DEFAULT_ALPHA = 1.2
DEFAULT_K = 16
DEFAULT_SEED_BASE = 10_000
DEFAULT_SF_LINES = 516
SF_ASSOC = 4

READ = "R"
WRITE = "W"

MEM_PREFIX = "Mem-"

assert HPA_SPAN <= HPA_SIZE, "workload spans more HPA than the region covers"
assert SHARED_BASE == HPA_SPAN // 2
assert P_BYTES <= S_BYTES


# ---------------------------------------------------------------------------
# Operations and workload container.
# ---------------------------------------------------------------------------

class Op(NamedTuple):
    """One trace line: 64-byte aligned address, access kind, issue tick."""

    addr: int
    kind: str
    tick: int


@dataclass(frozen=True)
class Workload:
    """The generated stream plus everything needed to reproduce it."""

    hosts: tuple[tuple[Op, ...], ...]
    meta: dict

    def host_ops(self, host: int) -> tuple[Op, ...]:
        return self.hosts[host]

    @property
    def total_ops(self) -> int:
        return sum(len(ops) for ops in self.hosts)


# ---------------------------------------------------------------------------
# Zipf sampling.
# ---------------------------------------------------------------------------

def zipf_weights(alpha: float, n: int = S_LINES) -> list[float]:
    """w[r] = 1 / (r + 1) ** alpha for r in [0, n)."""
    if n <= 0:
        raise ValueError("n must be positive")
    if alpha < 0:
        raise ValueError("alpha must be non-negative")
    return [1.0 / ((r + 1) ** alpha) for r in range(n)]


def zipf_cumulative(alpha: float, n: int = S_LINES) -> tuple[float, ...]:
    """Normalised cumulative weights; the last entry is exactly 1.0."""
    weights = zipf_weights(alpha, n)
    total = sum(weights)
    out: list[float] = []
    acc = 0.0
    for w in weights:
        acc += w
        out.append(acc / total)
    out[-1] = 1.0
    return tuple(out)


def sample_zipf(cumulative: Sequence[float], rng: random.Random) -> int:
    """Uniform draw inverted through the cumulative distribution."""
    u = rng.random()
    idx = bisect.bisect_left(cumulative, u)
    return min(idx, len(cumulative) - 1)


# ---------------------------------------------------------------------------
# Sharing set.
# ---------------------------------------------------------------------------

def publisher_pool(host: int, k: int) -> tuple[int, ...]:
    """Hosts whose shared instances host can read.

    k = 16 -> the whole fabric; k = 4 -> the four hosts of the caller's leaf.
    """
    if not 0 <= host < TOTAL_HOSTS:
        raise ValueError(f"host {host} out of range")
    if k == TOTAL_HOSTS:
        return tuple(range(TOTAL_HOSTS))
    if k == HOSTS_PER_LEAF:
        leaf = host // HOSTS_PER_LEAF
        return tuple(range(leaf * HOSTS_PER_LEAF, (leaf + 1) * HOSTS_PER_LEAF))
    raise ValueError(
        f"unsupported k={k}: the design fixes k in "
        f"{{{TOTAL_HOSTS}, {HOSTS_PER_LEAF}}}"
    )


# ---------------------------------------------------------------------------
# Generation.
# ---------------------------------------------------------------------------

def generate(
    *,
    ops_per_host: int = OPS_PER_HOST,
    shared_ratio: float = DEFAULT_SHARED_RATIO,
    private_phase_ratio: float = DEFAULT_PRIVATE_PHASE_RATIO,
    k: int = DEFAULT_K,
    alpha: float = DEFAULT_ALPHA,
    seed_base: int = DEFAULT_SEED_BASE,
    tick_stride: int = 1,
    line_offset: int = 0,
    private_lines: int = P_LINES,
    private_op_type: str = WRITE,
    private_ops: int | None = None,
    shared_ops: int | None = None,
    shared_write_ratio: float = 0.0,
    shared_homing: str = "pool",
    sharers: int | None = None,
) -> Workload:
    """Build all 16 host streams for one workload configuration.

    Each host has its own random.Random(seed_base + host), so changing one
    host's seed never perturbs the other hosts.  The draw order is fixed:
    publisher first, then the Zipf line inside that publisher.

    line_offset (in 64-byte lines) is added to every address after generation,
    private and shared alike, so the SF bucket index rotates by line_offset
    modulo the set count while the rest of the stream is untouched.  It must be
    a multiple of 64 lines: that keeps line % 64 (and therefore line % 16, the
    global 16-way endpoint) fixed.

    private_lines (1..S_LINES) overrides P_LINES: each host still issues the
    same private op budget, but cycles it over a shorter prefix of its private
    slot.  private_op_type is the op kind of both private runs (W by default).

    shared_write_ratio (0..1) turns a fraction of the shared ops into writes:
    each shared op draws W with that probability and R otherwise.  The draw
    happens only when the ratio is positive, so shared_write_ratio=0 consumes
    no extra randomness and reproduces the historical all-read stream
    bit-for-bit.  The shared stream is all reads when it is left at zero.

    sharers (1, 2, 4, 8, 16) replaces the publisher-slot shared layout with
    a cluster-strided one: hosts [c*n, (c+1)*n) form cluster c and draw their
    shared lines from a slice of CLUSTER_ROWS*n lines that is striped over
    exactly their own n memory endpoints.  Total shared footprint, per-endpoint
    shared op count (22,500 at the default ratio) and tracked lines per SF
    (CLUSTER_ROWS) are then identical for every n, so n moves one variable
    only: how many hosts hold a copy.  Requires mapping=global (16-way
    stripe), which is what the runner enforces.

    private_ops / shared_ops are an additive override: as soon as either is
    given, the per-host budget becomes their sum and both ops_per_host and
    shared_ratio are ignored.  A missing one counts as zero, so the total is
    always private_ops + shared_ops.  The private/shared split is then explicit
    (counts, not a ratio), which lets the shared stream grow without shrinking
    the private stream -- the additive sharing semantics the v3 draft used.

    shared_homing ("pool" | "leaf" | "remote") decouples who shares from where
    the shared data lives.  The slot a draw lands in is the shared instance's
    home, so the slot set is the homing knob: "pool" (default) draws from the
    k-slot publisher pool exactly as before -- with k=16 that is the whole
    fabric, with k=4 it is the caller's own leaf; "leaf" always draws from the
    caller's own four slots; "remote" draws from the four slots of the next
    leaf.  leaf and remote have the same sharer set, the same window count and
    the same Zipf shape, and differ only in whether the home endpoints are the
    readers' own -- local share 100% vs 0% -- so they isolate homing.  The
    default keeps the historical randrange modulus, so every pre-existing trace
    is bit-identical.
    """
    if not 0.0 <= shared_ratio <= 1.0:
        raise ValueError("shared_ratio must be in [0, 1]")
    if not 0.0 <= shared_write_ratio <= 1.0:
        raise ValueError("shared_write_ratio must be in [0, 1]")
    if shared_homing not in ("pool", "leaf", "remote"):
        raise ValueError(f"unsupported shared_homing={shared_homing!r}")
    if sharers is not None:
        if sharers not in (1, 2, 4, 8, 16) or TOTAL_HOSTS % sharers:
            raise ValueError(
                f"unsupported sharers={sharers!r}: expected a divisor of "
                f"{TOTAL_HOSTS} from (1, 2, 4, 8, 16)"
            )
    if not 0.0 <= private_phase_ratio <= 1.0:
        raise ValueError("private_phase_ratio must be in [0, 1]")
    if tick_stride <= 0:
        raise ValueError("tick_stride must be positive")
    if line_offset < 0:
        raise ValueError("line_offset must be non-negative")
    if line_offset % 64 != 0:
        raise ValueError(
            "line_offset must be a multiple of 64 lines so that line % 64 "
            "(host cache index) and line % 16 (global endpoint) stay fixed"
        )
    if not 1 <= private_lines <= S_LINES:
        raise ValueError(
            f"private_lines must be in [1, {S_LINES}], got {private_lines}"
        )
    if private_op_type not in (READ, WRITE):
        raise ValueError(f"private_op_type must be {READ!r} or {WRITE!r}, "
                         f"got {private_op_type!r}")
    offset_bytes = line_offset * BLOCK_SIZE
    p_lines = private_lines
    p_bytes = p_lines * BLOCK_SIZE

    # Two ways to size the per-host budget.  "ratio" is the historical mode;
    # "explicit" makes private and shared additive, so shared can be added on
    # top of a fixed private budget without diluting it.
    ops_mode = "explicit" if (private_ops is not None or shared_ops is not None) else "ratio"
    if ops_mode == "explicit":
        private_ops = 0 if private_ops is None else int(private_ops)
        shared_ops = 0 if shared_ops is None else int(shared_ops)
        if private_ops < 0 or shared_ops < 0:
            raise ValueError(
                f"private_ops and shared_ops must be non-negative, got "
                f"{private_ops} and {shared_ops}"
            )
        ops_per_host = private_ops + shared_ops
        if ops_per_host <= 0:
            raise ValueError(
                "private_ops + shared_ops must be positive, got "
                f"{private_ops} + {shared_ops}"
            )
        effective_ratio = shared_ops / ops_per_host
    else:
        if ops_per_host <= 0:
            raise ValueError("ops_per_host must be positive")
        shared_ops = int(ops_per_host * shared_ratio)
        private_ops = ops_per_host - shared_ops
        effective_ratio = shared_ratio
    private_pre = int(private_ops * private_phase_ratio)
    private_post = private_ops - private_pre

    cumulative = zipf_cumulative(alpha, S_LINES)
    # Sharer mode ranks the whole cluster slice (CLUSTER_ROWS * n lines), not
    # one 1,025-line slot: the cluster, not the publisher, is the unit that
    # owns and shares the window.
    shared_cumulative = (
        zipf_cumulative(alpha, CLUSTER_ROWS)
        if sharers is not None else cumulative
    )
    hosts: list[tuple[Op, ...]] = []
    max_addr = 0
    for host in range(TOTAL_HOSTS):
        rng = random.Random(seed_base + host)
        pool = publisher_pool(host, k)
        # The slot the shared draw lands in is the shared instance's home, so
        # the slot set is exactly the homing knob.  "pool" leaves it at the
        # k-slot sharer pool (len == k, so the randrange modulus and therefore
        # every historical trace is unchanged); "leaf" pins it to the caller's
        # own four endpoints; "remote" moves the same four windows one leaf
        # over, so the sharers and the footprint are unchanged and only the
        # distance of the home moves.
        if shared_homing == "leaf":
            slot_pool = publisher_pool(host, HOSTS_PER_LEAF)
        elif shared_homing == "remote":
            leaf = (host // HOSTS_PER_LEAF + 1) % LEAFS
            slot_pool = tuple(
                range(leaf * HOSTS_PER_LEAF, (leaf + 1) * HOSTS_PER_LEAF)
            )
        else:
            slot_pool = pool
        private_base = PRIVATE_BASE + host * PRIVATE_SLOT_BYTES
        if sharers is not None:
            # Hosts [c*n, (c+1)*n) form cluster c and read only the shared
            # lines homed at their own n endpoints.  n=1/4/16 reproduce the
            # span of the non/leaf/global mappings, but the tracked set per SF
            # (CLUSTER_ROWS lines) no longer shrinks as the span grows.
            cluster = host // sharers
            cluster_mems = tuple(
                range(cluster * sharers, (cluster + 1) * sharers)
            )
        ops: list[Op] = []
        scan = 0
        tick = 0

        for _ in range(private_pre):
            ops.append(
                Op(private_base + (scan % p_lines) * BLOCK_SIZE,
                   private_op_type, tick)
            )
            scan += 1
            tick += tick_stride

        for _ in range(shared_ops):
            if sharers is not None:
                # Popularity is a property of the row; the cluster spreads each
                # row evenly over its n endpoints.  Splitting by rank instead
                # (row = rank // n) would hand the whole Zipf head to mem 0 and
                # leave the last endpoint with a few percent of the traffic.
                row_i = sample_zipf(shared_cumulative, rng)
                mem = cluster_mems[rng.randrange(len(cluster_mems))]
                addr = SHARED_BASE + (row_i * TOTAL_MEMS + mem) * BLOCK_SIZE
            else:
                publisher = slot_pool[rng.randrange(len(slot_pool))]
                line = sample_zipf(cumulative, rng)
                addr = SHARED_BASE + publisher * SHARED_SLOT_BYTES + line * BLOCK_SIZE
            # The draw is guarded so that shared_write_ratio=0 consumes no
            # randomness: an unconditional rng.random() would shift every
            # later publisher/line draw and change the whole stream.
            kind = (
                WRITE
                if (shared_write_ratio > 0.0 and rng.random() < shared_write_ratio)
                else READ
            )
            ops.append(Op(addr, kind, tick))
            tick += tick_stride

        for _ in range(private_post):
            ops.append(
                Op(private_base + (scan % p_lines) * BLOCK_SIZE,
                   private_op_type, tick)
            )
            scan += 1
            tick += tick_stride

        if offset_bytes:
            # One constant shift over the finished stream: every host sees the
            # same offset, so relative structure (which host reads which line)
            # is unchanged and only the SF bucket assignment rotates.
            ops = [Op(addr + offset_bytes, kind, stamp) for addr, kind, stamp in ops]

        if len(ops) != ops_per_host:
            raise AssertionError(f"host {host}: {len(ops)} ops, expected {ops_per_host}")
        max_addr = max(max_addr, max(op.addr for op in ops))
        hosts.append(tuple(ops))

    if max_addr >= HPA_SIZE:
        raise AssertionError(
            f"max HPA 0x{max_addr:x} exceeds the 4 MiB region; requester folds "
            "the trace with addr % hpa_size and would wrap silently"
        )

    meta = {
        "workload_version": "wl2",
        "block_size": BLOCK_SIZE,
        "hosts": TOTAL_HOSTS,
        "mems": TOTAL_MEMS,
        "ops_per_host": ops_per_host,
        "total_ops": ops_per_host * TOTAL_HOSTS,
        "shared_ratio": effective_ratio,
        "shared_write_ratio": shared_write_ratio,
        "shared_ops": shared_ops,
        "private_ops": private_ops,
        "private_pre_ops": private_pre,
        "private_post_ops": private_post,
        "private_phase_ratio": private_phase_ratio,
        "k": k,
        "alpha": alpha,
        "seed_base": seed_base,
        "tick_stride": tick_stride,
        "line_offset": line_offset,
        "line_offset_bytes": offset_bytes,
        "s_lines": S_LINES,
        "s_bytes": S_BYTES,
        "p_lines": p_lines,
        "p_bytes": p_bytes,
        "private_op_type": private_op_type,
        "private_slot_bytes": PRIVATE_SLOT_BYTES,
        "shared_base": SHARED_BASE,
        "hpa_size": HPA_SIZE,
        "hpa_span": HPA_SPAN,
        "max_addr": max_addr,
    }
    if ops_mode == "explicit":
        # Recorded only for the additive override, so that a ratio-mode sanity
        # JSON stays byte-identical to the pre-knob one.
        meta["ops_mode"] = ops_mode
    if shared_homing != "pool":
        # Same rule as ops_mode: the historical meta stays byte-identical.
        meta["shared_homing"] = shared_homing
    if sharers is not None:
        meta["sharers"] = sharers
        meta["cluster_rows"] = CLUSTER_ROWS
        meta["shared_lines"] = CLUSTER_ROWS * TOTAL_MEMS
        meta["cluster_shared_lines"] = CLUSTER_ROWS * sharers
    return Workload(hosts=tuple(hosts), meta=meta)


# ---------------------------------------------------------------------------
# Address mapping: must be byte-for-byte the engine's query_flat/query_grouped.
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class FlatMapping:
    name: str
    granularity: int
    mem_indices: tuple[int, ...]
    hpa_start: int = 0
    hpa_size: int = HPA_SIZE


@dataclass(frozen=True)
class GroupSpec:
    mem_indices: tuple[int, ...]
    granularity: int


@dataclass(frozen=True)
class GroupedMapping:
    name: str
    group_granularity: int
    groups: tuple[GroupSpec, ...]
    hpa_start: int = 0
    hpa_size: int = HPA_SIZE


Mapping = Union[FlatMapping, GroupedMapping]


class Endpoint(NamedTuple):
    """Mirror of AddressSystem::QueryResult (dpa_base is 0 for every memory)."""

    dpa: int
    mem: int
    valid: bool


MAPPINGS: dict[str, Mapping] = {
    # Instance-level placement: one S_BYTES stripe per endpoint.
    "non": FlatMapping(
        name="non",
        granularity=S_BYTES,
        mem_indices=tuple(range(TOTAL_MEMS)),
    ),
    # Leaf-grouped: outer 4*S_BYTES over 4 leaf groups, inner 64 B over 4 mems.
    "leaf": GroupedMapping(
        name="leaf",
        group_granularity=HOSTS_PER_LEAF * S_BYTES,
        groups=tuple(
            GroupSpec(
                mem_indices=tuple(
                    range(leaf * MEMS_PER_LEAF, (leaf + 1) * MEMS_PER_LEAF)
                ),
                granularity=BLOCK_SIZE,
            )
            for leaf in range(LEAFS)
        ),
    ),
    # "non" variant B: private instance on the host's own endpoint, shared
    # instance evenly split over its leaf's four endpoints (32 slots -> 32
    # groups: private slot h -> group h, shared slot 16+h -> group 16+h).
    "non2": GroupedMapping(
        name="non2",
        group_granularity=S_BYTES,
        groups=tuple(
            [GroupSpec(mem_indices=(h,), granularity=S_BYTES) for h in range(TOTAL_HOSTS)]
            + [
                GroupSpec(
                    mem_indices=tuple(
                        range(
                            (h // HOSTS_PER_LEAF) * MEMS_PER_LEAF,
                            (h // HOSTS_PER_LEAF + 1) * MEMS_PER_LEAF,
                        )
                    ),
                    granularity=BLOCK_SIZE,
                )
                for h in range(TOTAL_HOSTS)
            ]
        ),
    ),
    # Same as non2 but the shared instance is split into four coarse chunks.
    "non3": GroupedMapping(
        name="non3",
        group_granularity=S_BYTES,
        groups=tuple(
            [GroupSpec(mem_indices=(h,), granularity=S_BYTES) for h in range(TOTAL_HOSTS)]
            + [
                GroupSpec(
                    mem_indices=tuple(
                        range(
                            (h // HOSTS_PER_LEAF) * MEMS_PER_LEAF,
                            (h // HOSTS_PER_LEAF + 1) * MEMS_PER_LEAF,
                        )
                    ),
                    granularity=S_BYTES // MEMS_PER_LEAF,
                )
                for h in range(TOTAL_HOSTS)
            ]
        ),
    ),
    # "non" variant A: whole instance striped in 4 KiB chunks over the host
    # leaf's four endpoints (coarse local interleave, no fine striping).
    "non4": GroupedMapping(
        name="non4",
        group_granularity=HOSTS_PER_LEAF * S_BYTES,
        groups=tuple(
            GroupSpec(
                mem_indices=tuple(
                    range(leaf * MEMS_PER_LEAF, (leaf + 1) * MEMS_PER_LEAF)
                ),
                granularity=4096,
            )
            for leaf in range(LEAFS)
        ),
    ),
    # Global 16-way at cache-line granularity.
    "global": FlatMapping(
        name="global",
        granularity=BLOCK_SIZE,
        mem_indices=tuple(range(TOTAL_MEMS)),
    ),
}

DEFAULT_MAPPINGS = tuple(MAPPINGS)


def resolve_mapping(mapping: Union[str, Mapping]) -> Mapping:
    if isinstance(mapping, str):
        try:
            return MAPPINGS[mapping]
        except KeyError:
            raise ValueError(
                f"unknown mapping {mapping!r}; expected one of "
                f"{sorted(MAPPINGS)}"
            ) from None
    if not isinstance(mapping, (FlatMapping, GroupedMapping)):
        raise TypeError(f"unsupported mapping object {mapping!r}")
    return mapping


def layout_endpoint(addr: int, mapping: Union[str, Mapping]) -> Endpoint:
    """Return the (dpa, mem) the engine would compute for addr.

    This is a literal transcription of AddressSystem::query_flat /
    query_grouped: same integer divisions, same modulo order, same rounding.
    Any difference here would make the sanity report lie about the run.
    """
    m = resolve_mapping(mapping)
    if addr < m.hpa_start or addr >= m.hpa_start + m.hpa_size:
        return Endpoint(0, -1, False)

    if isinstance(m, FlatMapping):
        offset = addr - m.hpa_start
        ways = len(m.mem_indices)
        stripe = offset // m.granularity
        target_idx = stripe % ways
        target_in_stripe = offset % m.granularity
        dpa = (stripe // ways) * m.granularity + target_in_stripe
        return Endpoint(dpa, m.mem_indices[target_idx], True)

    offset = addr - m.hpa_start
    n_groups = len(m.groups)
    group_stripe = offset // m.group_granularity
    group_idx = group_stripe % n_groups
    group = m.groups[group_idx]
    full_round_bytes = (group_stripe // n_groups) * m.group_granularity
    inner_offset = full_round_bytes + (offset % m.group_granularity)
    ways = len(group.mem_indices)
    inner_stripe = inner_offset // group.granularity
    target_idx = inner_stripe % ways
    dpa = (
        (inner_stripe // ways) * group.granularity
        + (inner_offset % group.granularity)
    )
    return Endpoint(dpa, group.mem_indices[target_idx], True)


def layout_mem(addr: int, mapping: Union[str, Mapping]) -> int:
    """Convenience wrapper: the memory index, or -1 when unmapped."""
    return layout_endpoint(addr, mapping).mem


def mem_name(mem_idx: int) -> str:
    return f"{MEM_PREFIX}{mem_idx}"


def address_system_toml(
    mapping: Union[str, Mapping],
    *,
    hpa_start: int = 0,
    hpa_size: int = HPA_SIZE,
) -> str:
    """Render the [address_system] section for one mapping."""
    m = resolve_mapping(mapping)
    if isinstance(m, FlatMapping):
        mems = ", ".join(f'"{mem_name(idx)}"' for idx in m.mem_indices)
        return (
            "[address_system]\n"
            f"hpa_start = {hpa_start}\n"
            f"hpa_size = {hpa_size}\n"
            f"ways = {len(m.mem_indices)}\n"
            f"granularity = {m.granularity}\n"
            f"memories = [{mems}]"
        )

    lines = [
        "[address_system]",
        "grouped = true",
        f"hpa_start = {hpa_start}",
        f"hpa_size = {hpa_size}",
        f"group_granularity = {m.group_granularity}",
    ]
    for group in m.groups:
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


# ---------------------------------------------------------------------------
# Snoop-filter indexing (snoop.hh:494): (addr / 64) % (line_num / assoc).
# ---------------------------------------------------------------------------

def snoop_set_count(line_num: int = DEFAULT_SF_LINES, assoc: int = SF_ASSOC) -> int:
    if line_num <= 0 or assoc <= 0 or line_num % assoc != 0:
        raise ValueError(
            f"line_num={line_num} must be a positive multiple of assoc={assoc}"
        )
    return line_num // assoc


def snoop_set_of(
    addr: int,
    line_num: int = DEFAULT_SF_LINES,
    assoc: int = SF_ASSOC,
) -> int:
    return (addr // BLOCK_SIZE) % snoop_set_count(line_num, assoc)
