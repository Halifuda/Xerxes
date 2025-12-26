from mkcfg import utils, devices
import argparse


parser = argparse.ArgumentParser(description="Sample bus config")
parser.add_argument("-s", "--scale", type=int, default=1, help="number of host/mem pairs")
parser.add_argument("-r", "--ratio", type=float, default=0.0, help="write ratio (float) for memory devices")
parser.add_argument("--log", type=str, default="output/sample-bus.csv", help="name of log file")
args = parser.parse_args()

SCALE = args.scale
RATIO = args.ratio
LOG = args.log

reqs = [devices.Requester(f"Host-{i}") for i in range(SCALE)]
mems = [devices.DRAMsim3Interface(f"Mem-{i}") for i in range(SCALE)]

switch1 = devices.Switch("Switch-1")
switch2 = devices.Switch("Switch-2")

bus = devices.DuplexBus("Bus")

for r in reqs:
    r.interleave_param = 500
    r.interleave_type = "random"
for m in mems:
    m.wr_ratio = RATIO
switch1.delay = 0 
switch2.delay = 0 # oracle switches, just gether all requests
# simulator currently assume a 64B payload in each request
# setting frame_size to 64 is to eliminate the framing overhead
# which shows the effect of duplex bus more clearly
bus.frame_size = 64

cfg = utils.Config()
cfg.log_name = LOG
cfg.add_devices(reqs)
cfg.add_devices(mems)
cfg.add_devices([switch1, switch2, bus])

for r in reqs:
    cfg.connect(r, switch1)
for m in mems:
    cfg.connect(switch2, m)
cfg.connect(switch1, bus)
cfg.connect(bus, switch2)

print(cfg)
