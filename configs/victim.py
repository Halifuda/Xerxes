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
Config.fill_parser(parser)
args = parser.parse_args()
cfg = Config(args)
cfg.log_level = "INFO"

host_num = 1
if args.outputdir == "fig14": host_num = 2
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
snp.line_num = host_num * cache_size // line_size
snp.max_burst_inv = args.burst_inv
snp.ranges = [[0, all_footprint]]
cfg.add_devices([snp])

hosts = []
for i in range(host_num):
    host = Requester()
    host.name = f"Host-{i}"
    host.q_capacity = 64
    host.coherent = True
    host.cache_capacity = cache_size
    if args.outputdir == "fig13":
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

if args.outputdir == "fig14":
    cfg.log_name = f"output/{args.outputdir}/len-{args.burst_inv}.csv"
else:
    cfg.log_name = f"output/{args.outputdir}/{args.policy}.csv"

if args.cfgname is not None:
    with open(args.cfgname, "w") as f:
        f.write(f"{cfg}")
else:
    print(cfg)
