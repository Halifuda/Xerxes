#pragma once
#include "dramsim3_interface.hh"
#include "requester.hh"
#ifndef XERXES_STANDALONE_HH
#define XERXES_STANDALONE_HH

#include "def.hh"
#include "device.hh"
#include "simulation.hh"
#include "utils.hh"

namespace xerxes {
struct XerxesConfig {
  // Max clock.
  Tick max_clock = 1000000;
  // Clock granularity.
  int clock_granu = 10;
  // Log name.
  std::string log_name = "output/try.csv";
  // Device list, <name, type>.
  std::map<std::string, std::string> devices;
  // Edge, <from, to>.
  std::vector<std::pair<std::string, std::string>> edges;
};

class Requester;
class DRAMsim3Interface;

struct XerxesContext {
  XerxesConfig general;
  std::map<std::string, TopoID> name_to_id;
  std::vector<Requester *> requesters;
  std::vector<DRAMsim3Interface *> mems;
};

void global_init(Simulation *sim, std::ostream &os, XerxesLogLevel level,
                 Packet::XerxesLoggerFunc pkt_logger);

Tick step();
bool events_empty();

XerxesContext parse_config(std::string config_file_name);
} // namespace xerxes

TOML11_DEFINE_CONVERSION_NON_INTRUSIVE(xerxes::XerxesConfig, max_clock,
                                       clock_granu, log_name, devices, edges);

#endif // XERXES_STANDALONE_HH
