class Device:
    def __init__(self, name="Device"):
        self.typename = "Device"
        self.name = name
        self.params = {}
    
    def __format__(self, fmt):
        return self.__str__()
    
    def __str__(self):
        res = ""
        res += f"[{self.name}]\n"
        for key, value in self.params.items():
            if type(value) == bool:
                res += f"{key} = {str(value).lower()}\n"
            elif type(value) == str:
                res += f"{key} = \"{value}\"\n"
            else:
                res += f"{key} = {value}\n"
        return res
    
    def __setattr__(self, name, value):
        if name == "typename" or name == "name" or name == "params":
            super().__setattr__(name, value)
        else:
            self.params[name] = value

    def __getattr__(self, name):
        if name == "typename" or name == "name" or name == "params":
            return super().__getattr__(name)
        else:
            return self.params[name]

class Requester(Device):
    def __init__(self, name="Requester"):
        self.typename = "Requester"
        self.name = name
        self.params = {
            "q_capacity": 32,
            "cache_capacity": 8192,
            "cache_delay": 12,
            "issue_delay": 0,
            "coherent": False,
            "burst_size": 1,
            "block_size": 64,
            "interleave_type": "stream",
            "interleave_param": 5,
            "trace_file": "",
        }

class DuplexBus(Device):
    def __init__(self, name="DuplexBus"):
        self.typename = "DuplexBus"
        self.name = name
        self.params = {
            "is_full": True,
            "half_rev_time": 100,
            "delay_per_T": 1,
            "width": 32,
            "framing_time": 20,
            "frame_size": 256,
        }

class DRAMsim3Interface(Device):
    def __init__(self, name="DRAMsim3Interface"):
        self.typename = "DRAMsim3Interface"
        self.name = name
        self.params = {
            "tick_per_clock": 1,
            "process_time": 40,
            "start": 0,
            "capacity": 1 << 30,
            "wr_ratio": 0.5,
            "config_file": "DRAMsim3/configs/DDR4_8Gb_x8_3200.ini",
            "output_dir": "output",
        }

class Snoop(Device):
    def __init__(self, name="Snoop"):
        self.typename = "Snoop"
        self.name = name
        self.params = {
            "line_num": 1024,
            "assoc": 8,
            "max_burst_inv": 8,
            "ranges": [[0, 1 << 30]],
            "eviction": "LRU",
        }

class Switch(Device):
    def __init__(self, name="Switch"):
        self.typename = "Switch"
        self.name = name
        self.params = {
            "delay": 1,
        }
