#pragma once
#include "def.hpp"
#include "device.hpp"

namespace xerxes {
class Switch : public Device {
private:
  Tick delay;
  std::unordered_map<TopoID, TopoID> upstreams;
  std::unordered_map<TopoID, TopoID> downstreams;
  std::unordered_map<TopoID, Timeline> downstream_queues;

  struct StreamInfo {
    TopoID up;
    TopoID down;
    bool is_down;
  };

  StreamInfo get_streams(const Packet &pkt) {
    auto src = pkt.src;
    auto dst = pkt.dst;
    if (upstreams.find(src) != upstreams.end()) {
      src = upstreams[src];
      ASSERT(downstreams.find(dst) != downstreams.end(),
             "No downstream for packet " + std::to_string(pkt.id) + " from " +
                 std::to_string(pkt.src) + " to " + std::to_string(pkt.dst));
      dst = downstreams[dst];
      return {src, dst, true};
    } else {
      ASSERT(downstreams.find(src) != downstreams.end(),
             "No downstream for packet " + std::to_string(pkt.id) + " from " +
                 std::to_string(pkt.src) + " to " + std::to_string(pkt.dst));
      src = downstreams[src];
      ASSERT(upstreams.find(dst) != upstreams.end(),
             "No upstream for packet " + std::to_string(pkt.id) + " from " +
                 std::to_string(pkt.src) + " to " + std::to_string(pkt.dst));
      dst = upstreams[dst];
      return {dst, src, false};
    }
  }

public:
  Switch(Topology *topology, Tick delay, std::string name = "Switch")
      : Device(topology, name), delay(delay) {}

  void add_upstream(TopoID host) {
    auto to = topology->next_node(self, host);
    if (to == nullptr) {
      return;
    }
    if (upstreams.find(host) == upstreams.end()) {
      upstreams[host] = to->id();
      // upstream_queues[host] = Timeline{};
    }
  }

  void add_downstream(TopoID ep) {
    auto to = topology->next_node(self, ep);
    if (to == nullptr) {
      return;
    }
    if (downstreams.find(ep) == downstreams.end()) {
      downstreams[ep] = to->id();
      downstream_queues[ep] = Timeline{};
    }
  }

  void transit() override {
    auto pkt = receive_pkt();

    if (pkt.dst == self) {
      return;
    }

    auto info = get_streams(pkt);
    if (info.is_down) {
      auto &queue = downstream_queues[info.down];
      auto transfer_time = queue.transfer_time(pkt.arrive, delay);

      if (pkt.arrive < transfer_time) {
        auto qd = transfer_time - pkt.arrive;
        pkt.delta_stat(SWITCH_QUEUE_DELAY, (double)(qd));
      }
      pkt.arrive = transfer_time;
    } // TODO: ignore upstream queuing
    pkt.delta_stat(SWITCH_TIME, (double)(delay));
    pkt.arrive += delay;
    log_transit_normal(pkt);
    send_pkt(pkt);
  }
};
} // namespace xerxes
