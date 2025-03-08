from mkcfg import utils, devices

SCALE = 1
RATIO = 0.0 # ratio must be set as float

reqs = [devices.Requester(f"Host-{i}") for i in range(SCALE)]
mems = [devices.DRAMsim3Interface(f"Mem-{i}") for i in range(SCALE)]

switch1 = devices.Switch("Switch-1")
switch2 = devices.Switch("Switch-2")

bus = devices.DuplexBus("Bus")

for r in reqs:
    r.interleave_param = 500
for m in mems:
    m.wr_ratio = RATIO
switch1.delay = 0 
switch2.delay = 0 # oracle switches, just gether all requests
# simulator currently assume a 64B payload in each request
# setting frame_size to 64 is to eliminate the framing overhead
# which shows the effect of duplex bus more clearly
bus.frame_size = 64

cfg = utils.Config()
cfg.log_name = f"output/sample-bus-{RATIO}.csv"
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
