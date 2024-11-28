class Config:
    def __init__(self, args=None):
        self.max_clock = 3000000
        self.clock_granu = 1
        self.log_level = "INFO"
        self.log_name = "output/default.csv"
        self.devices = {}
        self.connections = []

        if args is not None:
            self.parse_args(args)

    def fill_parser(parser):
        parser.add_argument("--max_clock", type=int, help="Maximum clock")
        parser.add_argument("--clock_granu", type=int, help="Clock granularity")
        parser.add_argument("--log_level", type=str, help="Log level")
        parser.add_argument("--log_name", type=str, help="Log name")

    def parse_args(self, args):
        if args.max_clock is not None:
            self.max_clock = args.max_clock
        if args.clock_granu is not None:
            self.clock_granu = args.clock_granu
        if args.log_level is not None:
            self.log_level = args.log_level
        if args.log_name is not None:
            self.log_name = args.log_name

    def add_devices(self, devices):
        for device in devices:
            self.devices[device.name] = device

    def connect(self, src, dst):
        self.connections.append((src, dst))

    def __format__(self, fmt):
        return self.__str__()

    def __str__(self):
        res = ""
        res += f"max_clock = {self.max_clock}\n"
        res += f"clock_granu = {self.clock_granu}\n"
        res += f"log_level = \"{self.log_level}\"\n"
        res += f"log_name = \"{self.log_name}\"\n"

        res += "edges = [\n"
        for src, dst in self.connections:
            res += f"    [\n"
            res += f"        \"{src.name}\",\n"
            res += f"        \"{dst.name}\",\n"
            res += f"    ],\n"
        res += "]\n\n"

        res += "[devices]\n"
        for key, value in self.devices.items():
            res += f"{key} = \"{value.typename}\"\n"
        res += "\n"

        for key, value in self.devices.items():
            res += f"{value}\n"
        return res
