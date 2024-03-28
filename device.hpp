#pragma once

#include "topo.hpp"

#include <string>

namespace xerxes {
class Device {
protected:
  Topology *topology;
  TopoID self;
  std::string name_;

  void send_pkt_to(Packet pkt, TopoID dst) {
    auto to = topology->next_node(self, dst);
    if (to == nullptr)
      return;
    pkt.from = self;
    to->send(pkt);
  }

  void send_pkt(Packet pkt) { send_pkt_to(pkt, pkt.dst); }

  Packet receive_pkt() {
    auto pkt = Packet{};
    topology->get_node(self)->receive(pkt);
    return pkt;
  }

  void show_all_pkt() {
    Logger::debug() << name() << " has packets: ";
    topology->get_node(self)->show_all_pkt();
  }

  void log_transit_normal(const Packet &pkt) {
    Logger::debug() << name() << " transit packet " << pkt.id << " from "
                    << pkt.from << " to " << pkt.dst << " at " << pkt.arrive
                    << std::endl;
  }

public:
  Device(Topology *topology, std::string name = "default_name")
      : topology(topology), self(topology->new_node()), name_(name) {}
  virtual ~Device() {}

  std::string name() const { return name_ + "#" + std::to_string(self); }
  const TopoID id() const { return self; }

  virtual void transit() {
    auto pkt = receive_pkt();
    if (pkt.dst == self) {
      return;
    }
    send_pkt(pkt);
  }

  virtual void log_stats(std::ostream &os) {}
};
} // namespace xerxes
