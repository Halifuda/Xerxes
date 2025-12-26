from mkcfg import utils, devices
import argparse


parser = argparse.ArgumentParser(description="Sample bus config")
parser.add_argument("-s", "--scale", type=int, default=8, help="number of host/mem pairs")
parser.add_argument("-t", "--topo", type=str, default="full", help="topology type (ring, chain, full)")
parser.add_argument("--log", type=str, default="output/sample-topo.csv", help="name of log file")
args = parser.parse_args()

SCALE = args.scale
TOPO = args.topo
LOG = args.log


reqs = [devices.Requester(f"Host-{i}") for i in range(SCALE)]
mems = [devices.DRAMsim3Interface(f"Mem-{i}") for i in range(SCALE)]

switches = [devices.Switch(f"Switch-{i}") for i in range(SCALE)]

for r in reqs:
    r.interleave_param = 500
for m in mems:
    m.wr_ratio = 0.0 # ratio must be set as float
for s in switches:
    s.delay = 25 # set port delay to N ns

cfg = utils.Config()
cfg.log_name = LOG
cfg.add_devices(reqs)
cfg.add_devices(mems)
cfg.add_devices(switches)

if TOPO == "ring":
    # req_i & mem_i -> switch_i, switch_[n] as a ring
    for i in range(SCALE):
        cfg.connect(reqs[i], switches[i])
        cfg.connect(switches[i], mems[i])
        if i > 0:
            cfg.connect(switches[i-1], switches[i])
    cfg.connect(switches[SCALE-1], switches[0])

elif TOPO == "chain":
    # req_i & mem_i -> switch_i, switch_[n] as a chain
    for i in range(SCALE):
        cfg.connect(reqs[i], switches[i])
        cfg.connect(switches[i], mems[i])
        if i > 0:
            cfg.connect(switches[i-1], switches[i])

elif TOPO == "full":
    # req_i -> switch_i -> mem_i, full connection
    for i in range(SCALE):
        cfg.connect(reqs[i], switches[i])
        cfg.connect(switches[i], mems[i])
    for i in range(SCALE):
        for j in range(SCALE):
            if i != j:
                cfg.connect(switches[i], switches[j])

print(cfg)
