#pragma once
#include "def.hpp"
#include "device.hpp"

#include <algorithm>
#include <map>

namespace xerxes {
class DuplexBus : public Device {
private:
  std::map<TopoID, std::map<TopoID, Tick>> routes;
  bool is_full;
  // Packet total delay = delay_per_T * size / width
  Tick delay_per_T;
  size_t width;
  size_t frame_size;
  Tick framing_time;

  std::map<std::string, double> stats;

  Tick get_or_init_route_tick(TopoID from, TopoID to, Tick current) {
    if (!is_full && from > to) {
      // shared by both directions
      std::swap(from, to);
    }
    if (routes.find(from) == routes.end()) {
      routes[from] = std::map<TopoID, Tick>();
      if (routes[from].find(to) == routes[from].end())
        routes[from][to] = current;
    }
    return routes[from][to];
  }

public:
  DuplexBus(Topology *topology, bool is_full, Tick delay_per_T, size_t width,
            Tick framing_time = 20, size_t frame_size = 256,
            std::string name = "DuplexBus")
      : Device(topology, name), is_full(is_full), delay_per_T(delay_per_T),
        width(width), frame_size(frame_size), framing_time(framing_time) {
    stats.insert(std::make_pair("Transfered_bytes", 0));
    stats.insert(std::make_pair("Transfered_payloads", 0));
  }

  void transit() override {
    auto pkt = receive_pkt();
    auto to = topology->next_node(self, pkt.dst);
    auto route_tick = get_or_init_route_tick(pkt.from, to->id(), pkt.arrive);
    size_t frame = (pkt.payload + frame_size - 1) / frame_size;
    auto delay = ((frame * frame_size + width - 1) / width) * delay_per_T;
    route_tick = std::max(route_tick, pkt.arrive);
    pkt.delta_stat(BUS_QUEUE_DELAY, (double)(route_tick - pkt.arrive));
    pkt.delta_stat(FRAMING_TIME, (double)framing_time);
    Logger::debug() << "[BQD #" << pkt.id << (pkt.is_rsp ? 'r' : ' ')
                    << "]: " << route_tick << " - " << pkt.arrive << " = "
                    << (double)(route_tick - pkt.arrive) << std::endl;
    pkt.delta_stat(BUS_TIME, (double)delay);

    pkt.arrive = route_tick + delay;
    routes[pkt.from][to->id()] = pkt.arrive;
    pkt.arrive += framing_time; // Donot include framing time in routing time

    stats["Transfered_bytes"] += frame * frame_size;
    stats["Transfered_payloads"] += pkt.payload;

    log_transit_normal(pkt);
    send_pkt(pkt);
  }

  void log_stats(std::ostream &os) override {
    os << "Bus stats: " << std::endl;
    for (auto &stat : stats) {
      os << std::fixed << stat.first << ": " << stat.second << std::endl;
    }
    os << "Efficiency: "
       << (double)stats["Transfered_payloads"] /
              (double)stats["Transfered_bytes"]
       << std::endl;
  }
};
} // namespace xerxes
