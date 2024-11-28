import mkcfg
from mkcfg import utils, devices

req = devices.Requester()
mem = devices.DRAMsim3Interface()
bus = devices.DuplexBus()
snp = devices.Snoop()

cfg = utils.Config()
cfg.add_devices([req, mem, bus, snp])
cfg.connect(req, bus)
cfg.connect(bus, snp)
cfg.connect(snp, mem)

print(cfg)
