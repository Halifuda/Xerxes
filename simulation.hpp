#pragma once
#include "def.hpp"
#include "system.hpp"
#include "topo.hpp"

// All devices.
#include "burst_packge.hpp"
#include "bus.hpp"
#include "cpu.hpp"
#include "dramsim3_interface.hpp"
#include "snoop.hpp"
#include "switch.hpp"

namespace xerxes {
class Simulation {
  Topology *p_topology;
  System *p_system;
  TopoID entry;

public:
  Simulation() : p_topology(new Topology), p_system(new System) {}

  ~Simulation() {
    delete p_topology;
    delete p_system;
  }

  Topology *topology() { return p_topology; }
  System *system() { return p_system; }
  void set_entry(TopoID id) { entry = id; }
};
} // namespace xerxes
