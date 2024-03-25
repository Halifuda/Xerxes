#pragma once
#include "def.hpp"
#include "device.hpp"

#include <algorithm>
#include <climits>
#include <map>

namespace xerxes {
class DuplexBus : public Device {
private:
  class Timeline {
  public:
    struct Scope {
      Tick start;
      Tick end;
      bool operator<(const Scope &rhs) const { return end < rhs.end; }
      Tick len() { return end > start ? end - start : 0; }
    };

    std::map<Tick, Scope> scopes;

    Tick transfer_time(Tick arrive, Tick delay) {
      Logger::debug() << "Timeline transfer time: " << arrive << ", delay "
                      << delay << std::endl;
      auto it = scopes.lower_bound(arrive);
      while (it->second.end - std::max(it->second.start, arrive) < delay &&
             it != scopes.end()) {
        Logger::debug() << "Skip scope " << it->second.start << "-"
                        << it->second.end << std::endl;
        it++;
      }
      ASSERT(it != scopes.end(), "Cannot find scope");
      Logger::debug() << "Use scope " << it->second.start << "-"
                      << it->second.end << std::endl;
      auto &scope = it->second;
      auto left = Scope{scope.start, std::max(scope.start, arrive)};
      auto right = Scope{std::max(scope.start, arrive) + delay, scope.end};
      auto ret = std::max(scope.start, arrive);
      scopes.erase(it);
      if (left.len() > 0) {
        Logger::debug() << "Insert new scope " << left.start << "-" << left.end
                        << std::endl;
        scopes[left.end] = left;
      }
      if (right.len() > 0) {
        Logger::debug() << "Insert new scope " << right.start << "-"
                        << right.end << std::endl;
        scopes[right.end] = right;
      }
      return ret;
    }
  };

  std::map<TopoID, std::map<TopoID, Timeline>> routes;
  bool is_full;
  Tick half_rev_time;
  // Packet total delay = delay_per_T * size / width
  Tick delay_per_T;
  size_t width;
  size_t frame_size;
  Tick framing_time;

  std::map<std::string, double> stats;

  Timeline &get_or_init_route(TopoID from, TopoID to) {
    if (!is_full && from > to) {
      // shared by both directions
      std::swap(from, to);
      stats["Direction reverse count"] += 1;
    }
    if (routes.find(from) == routes.end()) {
      routes[from] = std::map<TopoID, Timeline>();
      if (routes[from].find(to) == routes[from].end()) {
        routes[from][to] = Timeline{};
        routes[from][to].scopes.insert(
            std::make_pair(INT_MAX, Timeline::Scope{0, INT_MAX}));
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
  }

  void transit() override {
    show_all_pkt();
    auto pkt = receive_pkt();
    Logger::debug() << name_ << " transit packet " << pkt.id << std::endl;
    auto to = topology->next_node(self, pkt.dst);
    // absolute ceil (frames have some overheads)
    size_t frame = (pkt.payload + frame_size) / frame_size;
    if (pkt.is_sub_pkt) {
      // Sub packet is packaged with former, no need to add delay.
      pkt.is_sub_pkt = false;
      stats["Transfered_bytes"] += frame * frame_size;
      stats["Transfered_payloads"] += pkt.payload;
      log_transit_normal(pkt);
      send_pkt(pkt);
      return;
    }
    auto &route = get_or_init_route(pkt.from, to->id());
    auto delay = ((frame * frame_size + width - 1) / width) * delay_per_T;
    auto rev = is_full ? 0 : half_rev_time;
    auto transfer_time = route.transfer_time(pkt.arrive + rev, delay);

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
