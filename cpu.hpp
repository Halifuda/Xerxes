#pragma once

#include "def.hpp"
#include "device.hpp"
#include "utils.hpp"

#include <random>
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
      bool is_random;
      Addr cur;
    };
    std::vector<EndPoint> end_points;
    std::random_device rd;
    std::ranlux48 gen;
    std::uniform_real_distribution<> dis;
    size_t cur = 0;

  public:
    Interleaving() { dis = std::uniform_real_distribution<>(0, 1); }
    size_t size() { return end_points.size(); }
    void push_back(EndPoint ep) {
      ep.cur = ep.start;
      end_points.push_back(ep);
    }
    std::pair<TopoID, Addr> next() {
      auto id = end_points[cur].id;
      Addr addr = 0;
      if (end_points[cur].is_random) {
        addr =
            Addr(floor(double(end_points[cur].capacity) / 64 * dis(gen))) * 64 +
            end_points[cur].start;
      } else {
        addr = end_points[cur].cur;
        end_points[cur].cur += 64;
        if (end_points[cur].cur >=
            end_points[cur].start + end_points[cur].capacity)
          end_points[cur].cur = end_points[cur].start;
      }
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
  Tick last_arrive = 0;
  size_t count;
  size_t cur_cnt = 0;
  Tick delay;

public:
  Host(Topology *topology, size_t q_capacity, Tick snoop_time, size_t count,
       Tick delay, bool is_random = false, std::string name = "Host")
      : Device(topology, name), q(q_capacity), snoop_time(snoop_time),
        count(count), delay(delay) {}

  Host &add_end_point(TopoID id, Addr start, size_t capacity, bool is_random) {
    end_points.push_back({id, start, capacity, is_random});
    return *this;
  }

  void transit() override {
    auto pkt = receive_pkt();
    if (pkt.dst == self) {
      // On receive
      if (pkt.is_rsp) {
        Logger::debug() << name_ << " receive packet " << pkt.id
                        << ", issue queue is full? " << q.full() << std::endl;
        last_arrive = pkt.arrive;
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

  void log_stats(std::ostream &os) override {
    os << "Host stats: " << std::endl;
    os << "Issued packets: " << cur_cnt << std::endl;
    // TODO: cnt -> bytes
    os << "Bandwidth (GB/s): " << (double)(cur_cnt * 64) / (double)(last_arrive)
       << std::endl;
  }

  bool step(PacketType type) {
    if (cur_cnt < count) {
      if (q.full()) {
        if (cur < last_arrive)
          cur = last_arrive;
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
      cur_cnt++;
      return true;
    }
    return false;
  }

  bool all_issued() { return cur_cnt == count; }
  bool q_empty() { return q.empty(); }
};
} // namespace xerxes
