import argparse

from mkcfg.devices import *
from mkcfg.utils import *

parser = argparse.ArgumentParser(description="Generate a configuration file for a given topology")
parser.add_argument("--cfgname", type=str, help="Config file name")
parser.add_argument("--outputdir", type=str, help="Xerxes output (sub)directory")
parser.add_argument("--ratio", type=float, help="Write ratio")
parser.add_argument("--fsize", type=int, help="Frame size")
Config.fill_parser(parser)
args = parser.parse_args()
cfg = Config(args)

host = Requester(name="Host")
host.interleave_param = 20000
host.block_size = 64
mems = []
for i in range(4):
    mem = DRAMsim3Interface(name=f"Mem-{i}")
    mem.wr_ratio = args.ratio
    mems.append(mem)
bus = DuplexBus(name="Bus")
bus.width = 64
bus.frame_size = args.fsize
if args.fsize <= 0:
    bus.is_full = False
    bus.half_rev_time = 20
    bus.frame_size = 64
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

cfg.log_name = f"output/{args.outputdir}/{args.ratio}.csv"

if args.cfgname is not None:
    with open(args.cfgname, "w") as f:
        f.write(f"{cfg}")
else:
    print(cfg)
