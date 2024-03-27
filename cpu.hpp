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
    size_t block_size;

  public:
    Interleaving(size_t block_size = 64) : block_size(block_size) {
      dis = std::uniform_real_distribution<>(0, 1);
    }
    size_t size() { return end_points.size(); }
    void push_back(EndPoint ep) {
      ep.cur = ep.start;
      end_points.push_back(ep);
    }
    std::pair<TopoID, Addr> next() {
      auto id = end_points[cur].id;
      Addr addr = 0;
      if (end_points[cur].is_random) {
        addr = Addr(floor(double(end_points[cur].capacity) / block_size *
                          dis(gen))) *
                   block_size +
               end_points[cur].start;
      } else {
        addr = end_points[cur].cur;
        end_points[cur].cur += block_size;
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
  size_t burst_size = 1;
  size_t block_size = 64;

  std::unordered_map<TopoID, std::unordered_map<std::string, double>> stats;

public:
  Host(Topology *topology, size_t q_capacity, Tick snoop_time, size_t count,
       Tick delay, size_t burst_size = 1, std::string name = "Host")
      : Device(topology, name), q(q_capacity), snoop_time(snoop_time),
        count(count), delay(delay), burst_size(burst_size),
        block_size(burst_size * 64) {}

  Host &add_end_point(TopoID id, Addr start, size_t capacity, bool is_random) {
    end_points.push_back({id, start, capacity, is_random});
    stats[id] = {};
    stats[id]["Count"] = 0;
    stats[id]["Bandwidth"] = 0;
    stats[id]["Average latency"] = 0;
    stats[id]["Average wait on switch"] = 0;
    // stats[id]["Average wait on bus"] = 0;
    // stats[id]["Average wait for packaging"] = 0;
    // stats[id]["Average wait burst"] = 0;
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

        // Update stats
        stats[pkt.src]["Count"] += 1;
        stats[pkt.src]["Bandwidth"] += pkt.burst * 64;
        stats[pkt.src]["Average latency"] += pkt.arrive - pkt.sent;
        stats[pkt.src]["Average wait on switch"] +=
            pkt.get_stat(SWITCH_QUEUE_DELAY);

        q.pop(pkt);
        pkt.log_stat();
      } else if (pkt.type == INV) {
        std::swap(pkt.src, pkt.dst);
        pkt.is_rsp = true;
        pkt.payload = block_size;
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
    double agg_bw = 0;
    double agg_lat = 0;
    double agg_wait = 0;
    for (auto &pair : stats) {
      os << "  Endpoint " << pair.first << ": " << std::endl;
      os << "    Bandwidth (GB/s): "
         << pair.second["Bandwidth"] / (double)(last_arrive) << std::endl;
      agg_bw += pair.second["Bandwidth"] / (double)(last_arrive);

      os << "    Average latency (ns): "
         << pair.second["Average latency"] / pair.second["Count"] << std::endl;
      agg_lat += pair.second["Average latency"];
      os << "    Average wait on switch (ns): "
         << pair.second["Average wait on switch"] / pair.second["Count"]
         << std::endl;
      agg_wait += pair.second["Average wait on switch"];
    }
    os << "  Aggregate: " << std::endl;
    os << "    Bandwidth (GB/s): " << agg_bw << std::endl;
    os << "    Average latency (ns): " << agg_lat / cur_cnt << std::endl;
    os << "    Average wait on switch (ns): " << agg_wait / cur_cnt
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
      auto pkt =
          PktBuilder()
              .src(self)
              .dst(ep)
              .addr(addr)
              .sent(cur)
              .payload(type == PacketType::NT_WT || type == PacketType::WT
                           ? block_size
                           : 0)
              .burst(burst_size)
              .type(type)
              .build();
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
