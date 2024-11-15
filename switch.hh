#pragma once
#ifndef XERXES_SWITCH_HH
#define XERXES_SWITCH_HH

#include "device.hh"

#include <unordered_set>

namespace xerxes {
class SwitchConfig {
public:
  Tick delay = 1;
};
} // namespace xerxes

TOML11_DEFINE_CONVERSION_NON_INTRUSIVE(xerxes::SwitchConfig, delay);

namespace xerxes {
class Switch : public Device {
private:
  struct Port {
    TopoID id;
    std::unordered_map<TopoID, std::queue<Packet>> queues;
    std::unordered_map<TopoID, std::queue<Packet>>::iterator current;
    Timeline timeline;
    double sum_queue_depth = 0;
    double qd_record_cnt = 0;

    Packet next() {
      size_t i = 0;
      while (current->second.empty()) {
        current++;
        if (current == queues.end()) {
          current = queues.begin();
        }
        if (i++ > queues.size()) {
          return Packet{};
        }
      }
      auto pkt = current->second.front();
      current->second.pop();
      current++;
      if (current == queues.end()) {
        current = queues.begin();
      }
      return pkt;
    }
  };

  Tick delay;
  std::unordered_map<TopoID, std::pair<size_t, Tick>> upstreams;
  std::unordered_map<TopoID, Port> ports;

  Port &to_port(const Packet &pkt) {
    auto to = topology->next_node(self, pkt.dst);
    ASSERT(to != nullptr, name() + ": No next node for packet " +
                              std::to_string(pkt.id) + " from " +
                              std::to_string(pkt.src) + " to " +
                              std::to_string(pkt.dst));
    if (ports.find(to->id()) == ports.end()) {
      ports[to->id()] = Port{};
      ports[to->id()].id = to->id();
      for (auto &neighbor : topology->get_node(self)->neighbors()) {
        ports[to->id()].queues[neighbor] = std::queue<Packet>();
        ports[to->id()].current = ports[to->id()].queues.begin();
      }
    }
    return ports[to->id()];
  }

  void sched(Port &port) {
    auto pkt = port.next();
    if (!pkt.valid()) {
      return;
    }

    auto transfer_time = port.timeline.transfer_time(pkt.arrive, delay);
    if (transfer_time > pkt.arrive) {
      pkt.delta_stat(SWITCH_QUEUE_DELAY, (double)(transfer_time - pkt.arrive));
      pkt.arrive = transfer_time;
    }
    pkt.arrive += delay;
    pkt.delta_stat(SWITCH_TIME, (double)delay);

    log_transit_normal(pkt);
    send_pkt(pkt);
  }

public:
  Switch(Simulation *sim, const SwitchConfig &config,
         std::string name = "Switch")
      : Device(sim, name), delay(config.delay) {}

  void add_upstream(TopoID id, Tick delay) {
    upstreams.insert({id, {0, delay}});
  }

  void transit() override {
    const size_t wait_for_q = 4; // 1 means no queuing.
    auto pkt = receive_pkt();
    if (pkt.dst == self) {
      return;
    }
    auto &port = to_port(pkt);
    if (port.queues.find(pkt.from) == port.queues.end()) {
      port.queues[pkt.from] = std::queue<Packet>();
    }
    port.sum_queue_depth += port.queues[pkt.from].size();
    port.qd_record_cnt += 1;
    port.queues[pkt.from].push(pkt);
    if (upstreams.find(port.id) != upstreams.end()) {
      upstreams[port.id].first++;
      if (upstreams[port.id].first == wait_for_q) {
        upstreams[port.id].first = 0;
        for (size_t i = 0; i < wait_for_q; ++i)
          sched(port);
      }
    } else {
      sched(port);
    }
  }

  void log_stats(std::ostream &os) override {
    os << name() << " stats:\n";
    for (auto &port : ports) {
      if (upstreams.find(port.first) == upstreams.end())
        continue;
      os << "Port " << port.first << ":\n";
      os << "  Average queue depth: "
         << port.second.sum_queue_depth / port.second.qd_record_cnt << "\n";
    }
  }

  size_t port_num() const { return ports.size(); }
};

class DeviceBuffer : public Device {
private:
  size_t capacity;
  std::queue<Packet> pending;
  std::unordered_set<PktID> rd;
  std::unordered_set<PktID> wt;

public:
  DeviceBuffer(Simulation *sim, size_t capacity,
               std::string name = "DeviceBuffer")
      : Device(sim, name), capacity(capacity) {}

  void transit() override {
    auto pkt = receive_pkt();
    if (pkt.dst == self) {
      return;
    }
    if (!pkt.is_read() && !pkt.is_write()) {
      send_pkt(pkt);
    }

    std::unordered_set<PktID> &buf = pkt.is_read() ? rd : wt;
    if (pkt.is_rsp) {
      buf.erase(pkt.id);
      if (!pending.empty()) {
        auto next = pending.front();
        pending.pop();
        buf.insert(next.id);
        next.arrive = std::max(next.arrive, pkt.arrive);
        send_pkt(next);
      }
      send_pkt(pkt);
    } else {
      if (buf.size() < capacity) {
        buf.insert(pkt.id);
        send_pkt(pkt);
      } else {
        pending.push(pkt);
      }
    }
  }
};
} // namespace xerxes

#endif // XERXES_SWITCH_HH
