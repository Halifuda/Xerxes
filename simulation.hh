#pragma once
#ifndef XERXES_SIMULATION_HH
#define XERXES_SIMULATION_HH

#include "def.hh"
#include "event_log.hh"

namespace xerxes {
class AddressSystem;

class Simulation {
    Topology *p_topology;
    System *p_system;
    AddressSystem *_address_system = nullptr;
    std::vector<TimedEventLog *> event_logs_;

  public:
    Simulation();
    ~Simulation();

    Topology *topology() { return p_topology; }
    System *system() { return p_system; }
    AddressSystem *address_system() const { return _address_system; }
    void set_address_system(AddressSystem *as) { _address_system = as; }

    void register_event_log(TimedEventLog *log) {
        event_logs_.push_back(log);
    }
    void dump_event_logs(const std::string &base_path) const {
        for (auto *log : event_logs_) {
            if (log->empty()) continue;
            std::ofstream fout(base_path + "_" + log->name() + ".csv");
            log->write_csv(fout);
        }
    }
};
} // namespace xerxes

#endif // XERXES_SIMULATION_HH
