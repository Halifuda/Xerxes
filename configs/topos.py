import argparse

from mkcfg.devices import *
from mkcfg.utils import *

TOPOS = [
    "chain",
    "ring",
    "tree",
    "spineleaf",
    "full",
]

parser = argparse.ArgumentParser(description="Generate a configuration file for a given topology")
parser.add_argument("--topo", type=str, choices=TOPOS, help="Name of the topology")
parser.add_argument("--cfgname", type=str, help="Config file name")
parser.add_argument("--outputdir", type=str, help="Xerxes output (sub)directory")
parser.add_argument("--bw", type=float, help="switch port bandwidth (MB/s)")
parser.add_argument("--bus", action="store_true", help="Use buses to connect devices")
parser.add_argument("--norm", action="store_true", help="Use normalized bandwidth")
parser.add_argument("--epnum", type=int, help="Number of endpoints (host=mem)")
Config.fill_parser(parser)

args = parser.parse_args()
cfg = Config(args)
cfg.log_name = f"output/{args.outputdir}/{args.topo}.csv"

# assume switch byte length is 4
# then BW(MB/s) = Length / (Delay(s) * 1024**2) = Length * 1e9 / (Delay(ns) * 1024**2)
# Delay(ns) = Length * 1e9 / (BW(MB/s) * 1024**2)
def ceil(x):
    return int(x) + (x > int(x))
switch_delay = ceil(4e9 / (args.bw * 1024**2))
if args.norm:
    if args.topo == "chain":
        pass
    elif args.topo == "ring":
        switch_delay *= 2
    elif args.topo == "tree":
        pass
    elif args.topo == "spineleaf":
        # spineleaf: epnum * edge_switch, then a inf-bw switch connects all edge switches
        switch_delay *= ceil(args.epnum / 2)
        pass
    elif args.topo == "full":
        # full: epnum switches connect to each other
        switch_delay *= args.epnum

hosts = []
mems = []
switches = []
buses = []

for i in range(args.epnum):
    host = Requester(name=f"Host-{i}")
    host.q_capacity = 24
    host.interleave_param = 10000
    mem = DRAMsim3Interface(name=f"Mem-{i}")
    hosts.append(host)
    mems.append(mem)

cfg.add_devices(hosts)
cfg.add_devices(mems)

# Every edge switch connects to a host and a mem,
for i in range(args.epnum):
    switch = Switch(name=f"Switch-{i}")
    switch.delay = switch_delay
    switches.append(switch)
cfg.add_devices(switches)
for i in range(args.epnum):
    cfg.connect(hosts[i], switches[i])
    cfg.connect(switches[i], mems[i])

# then all switches connect to each other by the specified topology
# may need to add more switches to make the topology work
if args.topo == "chain":
    for i in range(args.epnum - 1):
        if args.bus:
            buses.append(DuplexBus(name=f"Bus-{i}"))
            buses[-1].width = 256
            cfg.add_devices([buses[-1]])
            cfg.connect(switches[i], buses[-1])
            cfg.connect(buses[-1], switches[i + 1])
        else:
            cfg.connect(switches[i], switches[i + 1])
elif args.topo == "ring":
    for i in range(args.epnum):
        if args.bus:
            buses.append(DuplexBus(name=f"Bus-{i}"))
            buses[-1].width = 256
            cfg.add_devices([buses[-1]])
            cfg.connect(switches[i], buses[-1])
            cfg.connect(buses[-1], switches[(i + 1) % args.epnum])
        else:
            cfg.connect(switches[i], switches[(i + 1) % args.epnum])
elif args.topo == "tree":
    # tree: each two switches connect to a new switch
    switch_num = args.epnum
    cur = 0
    while cur + 1 < switch_num:
        new_switch = Switch(name=f"Switch-{switch_num}")
        new_switch.delay = switch_delay
        cfg.add_devices([new_switch])
        switches.append(new_switch)
        if args.bus:
            buses.append(DuplexBus(name=f"Bus-{cur}"))
            buses[-1].width = 256
            cfg.add_devices([buses[-1]])
            cfg.connect(switches[cur], buses[-1])
            cfg.connect(buses[-1], new_switch)
            buses.append(DuplexBus(name=f"Bus-{cur + 1}"))
            buses[-1].width = 256
            cfg.add_devices([buses[-1]])
            cfg.connect(switches[cur + 1], buses[-1])
            cfg.connect(buses[-1], new_switch)
        else:
            cfg.connect(switches[cur], new_switch)
            cfg.connect(switches[cur + 1], new_switch)
        cur += 2
        switch_num += 1
elif args.topo == "spineleaf":
    # spineleaf: epnum * edge_switch, then a inf-bw switch connects all edge switches
    core = Switch(name="Core")
    core.delay = 0
    cfg.add_devices([core])
    for i in range(args.epnum):
        if args.bus:
            buses.append(DuplexBus(name=f"Bus-{i}"))
            buses[-1].width = 256
            cfg.add_devices([buses[-1]])
            cfg.connect(switches[i], buses[-1])
            cfg.connect(buses[-1], core)
        else:
            cfg.connect(switches[i], core)
elif args.topo == "full":
    for i in range(args.epnum):
        for j in range(i + 1, args.epnum):
            if args.bus:
                buses.append(DuplexBus(name=f"Bus-{i}-{j}"))
                buses[-1].width = 256
                cfg.add_devices([buses[-1]])
                cfg.connect(switches[i], buses[-1])
                cfg.connect(buses[-1], switches[j])
            else:
                cfg.connect(switches[i], switches[j])
if args.cfgname is not None:
    with open(args.cfgname, "w") as f:
        f.write(f"{cfg}")
else:
    print(cfg)
