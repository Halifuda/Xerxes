#pragma once
#ifndef XERXES_SIMULATION_HH
#define XERXES_SIMULATION_HH

#include "def.hh"

namespace xerxes {
class AddressSystem;

class Simulation {
    Topology *p_topology;
    System *p_system;
    AddressSystem *_address_system = nullptr;

  public:
    Simulation();
    ~Simulation();

    Topology *topology() { return p_topology; }
    System *system() { return p_system; }
    AddressSystem *address_system() const { return _address_system; }
    void set_address_system(AddressSystem *as) { _address_system = as; }
};
} // namespace xerxes

#endif // XERXES_SIMULATION_HH
