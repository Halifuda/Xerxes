#pragma once
#ifndef XERXES_DEVICE_HH
#define XERXES_DEVICE_HH

#include "def.hh"
#include "simulation.hh"
#include "system.hh"
#include "topology.hh"

namespace xerxes {
class Device {
protected:
  Simulation *sim;
  Topology *topology;
  TopoID self;
  std::string name_;

  void sched_transit(Tick tick);

  void send_pkt_to(Packet pkt, TopoID dst) {
    auto to = topology->next_node(self, dst);
    if (to == nullptr)
      return;
    pkt.from = self;
    to->send(pkt);
    auto a = sim->system()->find_dev(to->id());
    a->sched_transit(pkt.arrive);
  }

  void send_pkt(Packet pkt) { send_pkt_to(pkt, pkt.dst); }

  Packet receive_pkt() {
    auto pkt = Packet{};
    topology->get_node(self)->receive(pkt);
    return pkt;
  }

  void show_all_pkt() {
    XerxesLogger::debug() << name() << " has packets: ";
    topology->get_node(self)->show_all_pkt();
  }

  void log_transit_normal(const Packet &pkt) {
    XerxesLogger::debug() << name() << " transit packet " << pkt.id << " from "
                          << pkt.from << " to " << pkt.dst << " at "
                          << pkt.arrive << std::endl;
  }

public:
  Device(Simulation *sim, std::string name = "default_name")
      : sim(sim), self(sim->topology()->new_node()), name_(name) {
    topology = sim->topology();
  }
  virtual ~Device() {}

  std::string name() const { return name_ + "#" + std::to_string(self); }
  TopoID id() const { return self; }

  virtual void transit() {
    auto pkt = receive_pkt();
    XerxesLogger::warning()
        << "!DEFAULLT TRANSIT! " << name() << " received packet " << pkt.id
        << " from " << pkt.from << " to " << pkt.dst << " at " << pkt.arrive
        << std::endl;
    if (pkt.dst == self) {
      return;
    }
    send_pkt(pkt);
  }

  virtual void log_stats(std::ostream &os) {}

  auto get_transit_func() {
    return [this]() { transit(); };
  }
};
} // namespace xerxes

#endif // XERXES_DEVICE_HH
