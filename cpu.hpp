#pragma once

#include "def.hpp"
#include "device.hpp"
#include "utils.hpp"

#include <set>

namespace xerxes {

class Host : public Device {
private:
  class Interleaving {
  public:
    struct EndPoint {
      TopoID id;
      Addr start;
      size_t capacity;
      Addr cur;
    };
    std::vector<EndPoint> end_points;
    size_t cur = 0;

  public:
    Interleaving() {}
    size_t size() { return end_points.size(); }
    void push_back(EndPoint ep) {
      ep.cur = ep.start;
      end_points.push_back(ep);
    }
    bool has_next() {
      bool res = false;
      for (auto &ep : end_points) {
        res |= ep.cur < ep.start + ep.capacity;
      }
      return res;
    }
    std::pair<TopoID, Addr> next() {
      while (end_points[cur].cur >=
             end_points[cur].start + end_points[cur].capacity) {
        cur = (cur + 1) % end_points.size();
      }
      auto id = end_points[cur].id;
      auto addr = end_points[cur].cur;
      // TODO: step size
      end_points[cur].cur += 64;
      cur = (cur + 1) % end_points.size();
      return {id, addr};
    }
  };

  class IssueQueue {
    std::set<PktID> queue;
    size_t capacity;

  public:
    IssueQueue(size_t capacity) : capacity(capacity) {}
    bool full() { return queue.size() >= capacity; }
    bool empty() { return queue.empty(); }
    void push(const Packet &pkt) {
      if (full()) {
        Logger::warn() << "Queue is full!" << std::endl;
        return;
      }
      queue.insert(pkt.id);
    }
    void pop(const Packet &pkt) { queue.erase(pkt.id); }
  };

  Interleaving end_points;
  IssueQueue q;
  Tick snoop_time;
  Tick cur = 0;
  size_t count;
  Tick delay;

public:
  Host(Topology *topology, size_t q_capacity, Tick snoop_time, size_t count,
       Tick delay, std::string name = "Host")
      : Device(topology, name), q(q_capacity), snoop_time(snoop_time),
        count(count), delay(delay) {}

  Host &add_end_point(TopoID id, Addr start, size_t capacity) {
    end_points.push_back({id, start, capacity});
    return *this;
  }

  void transit() override {
    auto pkt = receive_pkt();
    if (pkt.dst == self) {
      // On receive
      if (pkt.is_rsp) {
        Logger::debug() << name_ << " receive packet " << pkt.id
                        << ", issue queue is full? " << q.full() << std::endl;
        if (q.full())
          cur = pkt.arrive;
        q.pop(pkt);
        pkt.log_stat();
      } else if (pkt.type == INV) {
        std::swap(pkt.src, pkt.dst);
        pkt.is_rsp = true;
        pkt.payload = 64;
        pkt.arrive += snoop_time;
        pkt.delta_stat(NormalStatType::HOST_INV_DELAY, snoop_time);
        send_pkt(pkt);
      }
      return;
    }
    log_transit_normal(pkt);
    send_pkt(pkt);
  }

  bool step(PacketType type) {
    if (count > 0 && end_points.has_next()) {
      if (q.full()) {
        return false;
      }
      auto pair = end_points.next();
      auto ep = pair.first;
      auto addr = pair.second;
      cur += delay;
      auto pkt = PktBuilder()
                     .src(self)
                     .dst(ep)
                     .addr(addr)
                     .sent(cur)
                     .payload(64)
                     .type(type)
                     .build();
      addr += 64;
      q.push(pkt);
      send_pkt(pkt);
      count--;
      return true;
    }
    return false;
  }

  bool all_issued() { return count == 0; }
  bool q_empty() { return q.empty(); }
};
} // namespace xerxes
