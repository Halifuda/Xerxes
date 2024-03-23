#pragma once
#include "def.hpp"
#include "device.hpp"

namespace xerxes {
class Switch : public Device {
private:
  Tick delay;
  Tick queuing = 0;

public:
  Switch(Topology *topology, Tick delay, std::string name = "Switch")
      : Device(topology, name), delay(delay) {}

  void transit() override {
    auto pkt = receive_pkt();

    if (pkt.dst == self) {
      return;
    }
    if (pkt.arrive < queuing) {
      auto qd = queuing - pkt.arrive;
      pkt.delta_stat(SWITCH_QUEUE_DELAY, (double)(qd));
      pkt.arrive = queuing;
    }
    pkt.delta_stat(SWITCH_TIME, (double)(delay));
    pkt.arrive += delay;

    log_transit_normal(pkt);
    send_pkt(pkt);
  }
};
} // namespace xerxes
