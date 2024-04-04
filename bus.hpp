#pragma once
#include "def.hpp"
#include "device.hpp"

#include <algorithm>
#include <map>

namespace xerxes {
class DuplexBus : public Device {
private:
  struct Route {
    Timeline timeline;
    std::map<Tick, bool> direction; // false: small to big
    Tick occupy = 0;
    Tick last_occupy = 0;
  };

  std::map<TopoID, std::map<TopoID, Route>> routes;
  bool is_full;
  Tick half_rev_time;
  // Packet total delay = delay_per_T * size / width
  Tick delay_per_T;
  size_t width;
  size_t frame_size;
  Tick framing_time;

  std::map<std::string, double> stats;

  Tick reverse_time(TopoID from, TopoID to, Tick arrive) {
    if (is_full)
      return 0;
    auto &route = get_or_init_route(from, to);
    auto &direction = route.direction;
    // TODO: rather approximate
    auto it = direction.lower_bound(arrive);

    bool direct = from > to;
    if (it->second != direct) {
      direction.insert({arrive, it->second});
      it->second = direct;
      stats["Direction reverse count"] += 1;
      return half_rev_time;
    }
    return 0;
  }

  Route &get_or_init_route(TopoID from, TopoID to) {
    if (!is_full && from > to) {
      // shared by both directions
      std::swap(from, to);
    }
    if (routes.find(from) == routes.end()) {
      routes[from] = std::map<TopoID, Route>();
      if (routes[from].find(to) == routes[from].end()) {
        routes[from][to] = {Timeline{}, std::map<Tick, bool>{}};
        routes[from][to].direction.insert({LONG_LONG_MAX, false});
      }
    }
    return routes[from][to];
  }

public:
  DuplexBus(Topology *topology, bool is_full, Tick half_rev_time,
            Tick delay_per_T, size_t width, Tick framing_time = 20,
            size_t frame_size = 256, std::string name = "DuplexBus")
      : Device(topology, name), is_full(is_full), half_rev_time(half_rev_time),
        delay_per_T(delay_per_T), width(width / 8),
        frame_size(frame_size), // width: bit -> byte
        framing_time(framing_time) {
    stats.insert(std::make_pair("Transfered_bytes", 0));
    stats.insert(std::make_pair("Transfered_payloads", 0));
    stats.insert(std::make_pair("Direction reverse count", 0));
    stats.insert(std::make_pair("Sent sub-packet count", 0));
    stats.insert(std::make_pair("Sent non-sub-packet count", 0));
  }

  void transit() override {
    auto pkt = receive_pkt();
    auto to = topology->next_node(self, pkt.dst);

    if (pkt.is_sub_pkt) {
      // Sub packet is packaged with former, no need to add delay.
      pkt.is_sub_pkt = false;
      stats["Transfered_payloads"] += pkt.payload;
      stats["Sent sub-packet count"] += 1;
      log_transit_normal(pkt);
      send_pkt(pkt);
      return;
    }

    // absolute ceil (frames have some overheads)
    size_t frame = (pkt.payload + frame_size) / frame_size;
    auto &route = get_or_init_route(pkt.from, to->id());
    auto delay = ((frame * frame_size + width - 1) / width) * delay_per_T;
    auto rev = reverse_time(pkt.from, to->id(), pkt.arrive);
    auto transfer_time = route.timeline.transfer_time(pkt.arrive + rev, delay);
    route.occupy += delay;
    route.last_occupy = std::max(route.last_occupy, pkt.arrive + rev + delay);

    pkt.delta_stat(BUS_QUEUE_DELAY, (double)(transfer_time - pkt.arrive));
    pkt.delta_stat(FRAMING_TIME, (double)framing_time);
    Logger::debug() << "[BQD #" << pkt.id << (pkt.is_rsp ? 'r' : ' ')
                    << "]: " << transfer_time << " - " << pkt.arrive << " = "
                    << (double)(transfer_time - pkt.arrive) << std::endl;
    pkt.delta_stat(BUS_TIME, (double)delay);

    pkt.arrive = transfer_time + delay;
    pkt.arrive += framing_time; // Donot include framing time in routing time

    stats["Transfered_bytes"] += frame * frame_size;
    stats["Transfered_payloads"] += pkt.payload;
    stats["Sent non-sub-packet count"] += 1;

    log_transit_normal(pkt);
    send_pkt(pkt);
  }

  void log_stats(std::ostream &os) override {
    os << name() << " stats: " << std::endl;
    for (auto &stat : stats) {
      os << std::fixed << stat.first << ": " << stat.second << std::endl;
    }
    os << "Efficiency: "
       << (double)stats["Transfered_payloads"] /
              (double)stats["Transfered_bytes"]
       << std::endl;
    double utils = 0;
    double cnt = 0;
    for (auto &from : routes) {
      for (auto &to : from.second) {
        utils += (double)to.second.occupy / (double)to.second.last_occupy;
        cnt += 1;
      }
    }
    os << "Average utilization: " << utils / cnt << std::endl;
  }

  // TODO: TEMP
  double avg_utilization() {
    double utils = 0;
    double cnt = 0;
    for (auto &from : routes) {
      for (auto &to : from.second) {
        utils += (double)to.second.occupy / (double)to.second.last_occupy;
        cnt += 1;
      }
    }
    return utils / cnt;
  }

  // TODO: TEMP
  double efficiency() {
    return (double)stats["Transfered_payloads"] /
           (double)stats["Transfered_bytes"];
  }
};
} // namespace xerxes
