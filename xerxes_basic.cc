#include "device.hh"
#include "simulation.hh"
#include "system.hh"
#include "topology.hh"

namespace xerxes {
Simulation::Simulation() {
    p_topology = new Topology();
    p_system = new System();
}

Simulation::~Simulation() {
    delete p_topology;
    delete p_system;
}

System *System::add_dev(Device *device) {
    devices.insert({device->id(), device});
    return this;
}

Device *System::find_dev(TopoID id) {
    auto it = devices.find(id);
    if (it == devices.end()) {
        return nullptr;
    }
    return it->second;
}
} // namespace xerxes
