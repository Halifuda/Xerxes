import argparse

from mkcfg.devices import *
from mkcfg.utils import *

parser = argparse.ArgumentParser(description="Generate a configuration file for a given topology")
parser.add_argument("--cfgname", type=str, help="Config file name")
parser.add_argument("--outputdir", type=str, help="Xerxes output (sub)directory")
parser.add_argument("--trace", type=str, help="Write ratio")
parser.add_argument("--work", type=str, help="Work type")
Config.fill_parser(parser)
args = parser.parse_args()
cfg = Config(args)

tracefile = f"./traces/{args.trace}.trace"

if args.work == "fullbus":
    host = Requester(name="Host")
    host.interleave_type = "trace"
    host.trace_file = tracefile
    host.block_size = 64
    mems = []
    for i in range(4):
        mem = DRAMsim3Interface(name=f"Mem-{i}")
        mem.capacity = 1 << 62
        mems.append(mem)
    bus = DuplexBus(name="Bus")
    bus.is_full = True
    bus.width = 64
    bus.frame_size = 32
    switch = Switch(name="OracleSwitch")
    switch.delay = 0
    cfg.add_devices([host])
    cfg.add_devices(mems)
    cfg.add_devices([bus])
    cfg.add_devices([switch])
    cfg.connect(host, bus)
    cfg.connect(bus, switch)
    for mem in mems:
        cfg.connect(switch, mem)

elif args.work == "halfbus":
    host = Requester(name="Host")
    host.interleave_type = "trace"
    host.trace_file = tracefile
    host.block_size = 64
    mems = []
    for i in range(4):
        mem = DRAMsim3Interface(name=f"Mem-{i}")
        mem.capacity = 1 << 62
        mems.append(mem)
    bus = DuplexBus(name="Bus")
    bus.is_full = False
    bus.half_rev_time = 0
    bus.width = 64
    bus.frame_size = 32
    switch = Switch(name="OracleSwitch")
    switch.delay = 0
    cfg.add_devices([host])
    cfg.add_devices(mems)
    cfg.add_devices([bus])
    cfg.add_devices([switch])
    cfg.connect(host, bus)
    cfg.connect(bus, switch)
    for mem in mems:
        cfg.connect(switch, mem)

elif args.work in ["chain", "ring", "tree", "spineleaf", "full"]:
    epnum = 8
    hosts = []
    mems = []
    switches = []
    buses = []

    for i in range(epnum):
        host = Requester(name=f"Host-{i}")
        host.q_capacity = 4
        host.interleave_type = "trace"
        host.trace_file = tracefile
        host.block_size = 64
        mem = DRAMsim3Interface(name=f"Mem-{i}")
        hosts.append(host)
        mems.append(mem)

    cfg.add_devices(hosts)
    cfg.add_devices(mems)

    for i in range(epnum):
        switch = Switch(name=f"Switch-{i}")
        switch.delay = 1
        switches.append(switch)
    cfg.add_devices(switches)

    for i in range(epnum):
        cfg.connect(hosts[i], switches[i])
        cfg.connect(switches[i], mems[i])

    if args.work == "chain":
        for i in range(epnum - 1):
            bus = DuplexBus(name=f"Bus-{i}")
            bus.width = 256
            cfg.add_devices([bus])
            buses.append(bus)
            cfg.connect(switches[i], bus)
            cfg.connect(bus, switches[i + 1])
    elif args.work == "ring":
        for i in range(epnum):
            bus = DuplexBus(name=f"Bus-{i}")
            bus.width = 256
            cfg.add_devices([bus])
            buses.append(bus)
            cfg.connect(switches[i], bus)
            cfg.connect(bus, switches[(i + 1) % epnum])
    elif args.work == "tree":
        switch_num = epnum
        cur = 0
        while cur + 1 < switch_num:
            new_switch = Switch(name=f"Switch-{switch_num}")
            new_switch.delay = 1
            cfg.add_devices([new_switch])
            switches.append(new_switch)
            b1 = DuplexBus(name=f"Bus-{cur}")
            b1.width = 256
            cfg.add_devices([b1])
            cfg.connect(switches[cur], b1)
            cfg.connect(b1, new_switch)
            b2 = DuplexBus(name=f"Bus-{cur + 1}")
            b2.width = 256
            cfg.add_devices([b2])
            cfg.connect(switches[cur + 1], b2)
            cfg.connect(b2, new_switch)
            cur += 2
            switch_num += 1
    elif args.work == "spineleaf":
        core = Switch(name="Core")
        core.delay = 0
        cfg.add_devices([core])
        for i in range(epnum):
            bus = DuplexBus(name=f"Bus-{i}")
            bus.width = 256
            cfg.add_devices([bus])
            buses.append(bus)
            cfg.connect(switches[i], bus)
            cfg.connect(bus, core)
    elif args.work == "full":
        for i in range(epnum):
            for j in range(i + 1, epnum):
                bus = DuplexBus(name=f"Bus-{i}-{j}")
                bus.width = 256
                cfg.add_devices([bus])
                buses.append(bus)
                cfg.connect(switches[i], bus)
                cfg.connect(bus, switches[j])

cfg.log_name = f"output/{args.outputdir}/{args.trace}.csv"
if args.cfgname is not None:
    with open(args.cfgname, "w") as f:
        f.write(f"{cfg}")
else:
    print(cfg)
