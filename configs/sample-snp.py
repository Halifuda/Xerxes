from mkcfg import utils, devices
import argparse



parser = argparse.ArgumentParser(description="Sample snoop config")
parser.add_argument("-s", "--scale", type=int, default=1, help="number of host/mem pairs")
parser.add_argument("-e", "--evict", type=str, default="FIFO", help="eviction policy for snoop device")
parser.add_argument("--log", type=str, default="output/sample-snp.csv", help="name of log file")
args = parser.parse_args()

SCALE = args.scale
LOG = args.log
EVICT = args.evict

reqs = [devices.Requester(f"Host-{i}") for i in range(SCALE)]
mem = devices.DRAMsim3Interface("Mem")
switch = devices.Switch("Switch")
bus = devices.DuplexBus("Bus")
snoop = devices.Snoop("Snoop")

for r in reqs:
    r.interleave_param = 5000
    r.coherent = True
mem.wr_ratio = 0.0
switch.delay = 0 # oracle switches, just gether all requests
snoop.eviction = EVICT
snoop.ranges = []
snoop.ranges.append([mem.start, mem.start + mem.capacity])

cfg = utils.Config()
cfg.log_name = LOG
cfg.add_devices(reqs)
cfg.add_devices([mem, switch, bus, snoop])

for r in reqs:
    cfg.connect(r, switch)
cfg.connect(switch, bus)
cfg.connect(bus, snoop)
cfg.connect(snoop, mem)

print(cfg)
