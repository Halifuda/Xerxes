#pragma once
#ifndef XERXES_SIMULATION_HH
#define XERXES_SIMULATION_HH

#include "def.hh"

namespace xerxes {
class Simulation {
    Topology *p_topology;
    System *p_system;

  public:
    Simulation();
    ~Simulation();

    Topology *topology() { return p_topology; }
    System *system() { return p_system; }
};
} // namespace xerxes

#endif // XERXES_SIMULATION_HH
