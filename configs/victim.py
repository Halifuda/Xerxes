import argparse

from mkcfg.devices import *
from mkcfg.utils import *

POLICIES = [
    "FIFO",
    "LIFO",
    "LFI",
    "LRU",
    "MRU"
]

parser = argparse.ArgumentParser(description="Generate a configuration file for a given topology")
parser.add_argument("--policy", type=str, choices=POLICIES, help="Victim select policy")
parser.add_argument("--burst_inv", type=int, default=0, help="Max burst invalidations")
parser.add_argument("--cfgname", type=str, help="Config file name")
parser.add_argument("--outputdir", type=str, help="Xerxes output (sub)directory")
parser.add_argument("--workload", type=str, choices=["fig13", "fig14"], default=None,
                    help="Workload type: fig13 (hotcold, 1 host) or fig14 (stream, 2 hosts); "
                         "inferred from --outputdir when omitted")
parser.add_argument("--clean_evict", action="store_true", default=False,
                    help="Emit MemClnEvct notifications for host clean evictions")
parser.add_argument("--cache_lines", type=int, default=None,
                    help="Override host.cache_capacity (in cache lines)")
parser.add_argument("--sf_lines", type=int, default=None,
                    help="Override Snoop.line_num (in cache lines)")
parser.add_argument("--wr_ratio", type=float, default=None,
                    help="Override host write ratio (fraction of writes in the hot/cold stream)")
parser.add_argument("--seed_base", type=int, default=10000,
                    help="Base random seed; host i uses seed_base + i")
Config.fill_parser(parser)
args = parser.parse_args()
cfg = Config(args)
cfg.log_level = "INFO"

workload = args.workload
if workload is None:
    workload = "fig14" if args.outputdir == "fig14" else "fig13"

host_num = 1
if workload == "fig14": host_num = 2
hot_req_ratio = 0.9
hot_region_ratio = 1 - hot_req_ratio
all_footprint = 1024 * 1024
line_size = 64
assoc = 8
raw_hot = int(all_footprint * hot_region_ratio)
align = line_size * assoc
hot_footprint = max(align, ((raw_hot + align - 1) // align) * align)
cache_size = min(all_footprint, hot_footprint * 2)

snp = Snoop(name="Snoop")
snp.eviction = args.policy
snp.line_num = args.sf_lines if args.sf_lines is not None else host_num * cache_size // line_size
snp.max_burst_inv = args.burst_inv
snp.ranges = [[0, all_footprint]]
cfg.add_devices([snp])

hosts = []
for i in range(host_num):
    host = Requester()
    host.name = f"Host-{i}"
    host.random_seed = args.seed_base + i
    host.q_capacity = 64
    host.coherent = True
    host.cache_capacity = args.cache_lines if args.cache_lines is not None else cache_size // line_size
    host.clean_evict_notify = args.clean_evict
    if args.wr_ratio is not None:
        host.wr_ratio = args.wr_ratio
    host.hpa_base = 0
    host.hpa_size = all_footprint
    if workload == "fig13":
        host.interleave_type = "hotcold"
    else:
        host.interleave_type = "stream"
    host.hot_req_ratio = hot_req_ratio
    host.hot_region_ratio = hot_region_ratio
    host.issue_delay = 2
    host.interleave_param = 30000
    hosts.append(host)
mem0 = DRAMsim3Interface(name="Mem-0")
mem0.wr_ratio = 0.0
mem0.capacity = all_footprint
switch = Switch(name="OracleSwitch")
switch.delay = 0
bus = DuplexBus(name="OracleBus")
bus.delay_per_T = 0
bus.width = 256
bus.frame_size = 32
cfg.add_devices(hosts)
cfg.add_devices([mem0])
cfg.add_devices([switch, bus])
for host in hosts:
    cfg.connect(host, switch)
cfg.connect(switch, bus)
cfg.connect(bus, snp)
cfg.connect(snp, mem0)

if workload == "fig14":
    cfg.log_name = f"output/{args.outputdir}/len-{args.burst_inv}.csv"
else:
    cfg.log_name = f"output/{args.outputdir}/{args.policy}.csv"

if args.cfgname is not None:
    with open(args.cfgname, "w") as f:
        f.write(f"{cfg}")
else:
    print(cfg)
