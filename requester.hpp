#pragma once

#include "def.hpp"
#include "device.hpp"
#include "utils.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <list>
#include <random>
#include <set>
#include <unordered_set>

namespace xerxes {

class Requester : public Device {
public:
  class Interleaving {
  protected:
    std::random_device rd;
    std::ranlux48 gen;
    std::uniform_real_distribution<> uni;

  public:
    struct Request {
      TopoID id;
      Addr addr;
      Tick tick;
      bool is_write;
    };

    struct EndPoint {
      TopoID id;
      Addr start;
      size_t capacity;
      double ratio;
      Addr cur;
    };
    std::vector<EndPoint> end_points;
    size_t cur = 0;
    size_t block_size;

  public:
    Interleaving(size_t block_size = 64) : block_size(block_size) {
      uni = std::uniform_real_distribution<>(0, 1);
    }
    size_t size() { return end_points.size(); }
    void push_back(EndPoint ep) {
      ep.cur = ep.start;
      end_points.push_back(ep);
    }

    virtual Request next() = 0;
  };

private:
  class FakeLRUCache {
    std::list<Addr> cache;
    size_t capacity;

  public:
    Tick delay;
    FakeLRUCache(size_t capacity, Tick delay)
        : capacity(capacity), delay(delay) {}
    void insert(Addr addr) {
      if (cache.size() >= capacity)
        cache.pop_front();
      cache.push_back(addr);
    }
    bool hit(Addr addr) {
      auto it = std::find(cache.begin(), cache.end(), addr);
      if (it != cache.end()) {
        cache.erase(it);
        cache.push_back(addr);
        return true;
      }
      return false;
    }
    void invalidate(Addr addr) {
      auto it = std::find(cache.begin(), cache.end(), addr);
      if (it != cache.end())
        cache.erase(it);
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

  Interleaving *end_points;
  IssueQueue q;
  FakeLRUCache cache;
  Tick cur = 0;
  Tick last_arrive = 0;
  size_t count;
  size_t cur_cnt = 0;
  Tick delay;
  size_t burst_size = 1;
  size_t block_size = 64;

  std::unordered_map<TopoID, std::unordered_map<std::string, double>> stats;

public:
  Requester(Topology *topology, size_t q_capacity, size_t cache_capacity,
            Tick cache_delay, size_t count, Tick delay, size_t burst_size = 1,
            Interleaving *interleave = nullptr, std::string name = "Host")
      : Device(topology, name), end_points(interleave), q(q_capacity),
        cache(cache_capacity, cache_delay), count(count), delay(delay),
        burst_size(burst_size), block_size(burst_size * 64) {
    ASSERT(end_points != nullptr, "No interleave policy");
  }

  Requester &add_end_point(TopoID id, Addr start, size_t capacity,
                           double ratio) {
    end_points->push_back({id, start, capacity, ratio});
    stats[id] = {};
    stats[id]["Count"] = 0;
    stats[id]["Bandwidth"] = 0;
    stats[id]["Average latency"] = 0;
    stats[-1]["Cache evict count"] = 0;
    stats[-1]["Cache hit count"] = 0;
    // stats[id]["Average switch queuing"] = 0;
    // stats[id]["Average switch time"] = 0;
    stats[id]["Average wait for evict"] = 0;
    // stats[-1]["Invalidation count"] = 0;
    // stats[id]["Average wait on switch"] = 0;
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
        Logger::debug() << name() << " receive packet " << pkt.id
                        << ", issue queue is full? " << q.full() << std::endl;
        last_arrive = pkt.arrive;
        cache.insert(pkt.addr);

        // Update stats
        stats[pkt.src]["Count"] += 1;
        stats[pkt.src]["Bandwidth"] += pkt.burst * 64;
        stats[pkt.src]["Average latency"] += pkt.arrive - pkt.sent;
        stats[pkt.src]["Average wait for evict"] +=
            pkt.get_stat(SNOOP_EVICT_DELAY);

        q.pop(pkt);
        pkt.log_stat();
      } else if (pkt.type == INV) {
        cache.invalidate(pkt.addr);
        stats[-1]["Cache evict count"] += 1;
        std::swap(pkt.src, pkt.dst);
        pkt.is_rsp = true;
        pkt.payload = block_size * pkt.burst;
        pkt.arrive += cache.delay; // TODO: one or each?
        pkt.delta_stat(NormalStatType::HOST_INV_DELAY, cache.delay);
        send_pkt(pkt);
      }
      return;
    }
    log_transit_normal(pkt);
    send_pkt(pkt);
  }

  double log_stats_csv(std::ostream &os) {
    os << name() << " stats: " << std::endl;
    double agg_bw = 0;
    double agg_cnt = 0;
    double agg_lat = 0;
    os << std::fixed << std::setprecision(4);
    for (auto &pair : stats) {
      if (pair.first == -1)
        continue;
      os << pair.first << ","
         << pair.second["Bandwidth"] / (double)(last_arrive) << ","
         << pair.second["Average latency"] / pair.second["Count"] << std::endl;

      agg_cnt += pair.second["Count"];
      agg_bw += pair.second["Bandwidth"] / (double)(last_arrive);
      agg_lat += pair.second["Average latency"];
    }
    os << "Aggregate," << agg_bw << "," << agg_lat / agg_cnt << ","
       << std::endl;
    os << "#Evict"
       << "," << stats[-1]["Cache evict count"] << std::endl;
    return agg_bw;
  }

  void log_stats(std::ostream &os) override {
    os << name() << " stats: " << std::endl;
    os << " * Issued packets: " << cur_cnt << std::endl;
    os << " * Evict count: " << stats[-1]["Cache evict count"] << std::endl;
    os << " * Hit count: " << stats[-1]["Cache hit count"] << std::endl;
    double agg_bw = 0;
    double agg_cnt = 0;
    double agg_lat = 0;
    double agg_wait = 0;
    for (auto &pair : stats) {
      if (pair.first == -1)
        continue;
      agg_cnt += pair.second["Count"];
      os << " * Endpoint " << pair.first << ": " << std::endl;
      os << "   - Bandwidth (GB/s): "
         << pair.second["Bandwidth"] * 1000 / (double)(last_arrive)
         << std::endl;
      agg_bw += pair.second["Bandwidth"] * 1000 / (double)(last_arrive);

      os << "   - Average latency (ps): "
         << pair.second["Average latency"] / pair.second["Count"] << std::endl;
      agg_lat += pair.second["Average latency"];

      os << "   - Average wait for evict (ps): "
         << pair.second["Average wait for evict"] / pair.second["Count"]
         << std::endl;
      agg_wait += pair.second["Average wait for evict"];
    }
    os << " * Aggregate: " << std::endl;
    os << "   - Bandwidth (GB/s): " << agg_bw << std::endl;
    os << "   - Average latency (ps): " << agg_lat / agg_cnt << std::endl;
    os << "   - Average wait for evict (ps): " << agg_wait / agg_cnt
       << std::endl;
  }

  bool step(bool coherent) {
    if (cur_cnt < count) {
      if (q.full()) {
        if (cur < last_arrive)
          cur = last_arrive;
        return false;
      }
      auto req = end_points->next();
      auto ep = req.id;
      auto addr = req.addr;
      cur += delay;
      if (req.tick != 0)
        cur = req.tick;
      if (cache.hit(addr)) {
        stats[ep]["Count"] += 1;
        stats[ep]["Bandwidth"] += burst_size * 64;
        stats[ep]["Average latency"] += cache.delay;
        stats[-1]["Cache hit count"] += 1;
        cur += cache.delay;
        return true;
      }
      auto type = req.is_write
                      ? (coherent ? PacketType::WT : PacketType::NT_WT)
                      : (coherent ? PacketType::RD : PacketType::NT_RD);
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

  class Trace : public Interleaving {
  private:
    std::ifstream trace_file;

  public:
    Trace(std::string trace_file, size_t block_size = 64)
        : Interleaving(block_size) {
      this->trace_file.open(trace_file);
      if (!this->trace_file.is_open()) {
        Logger::error() << "Cannot open trace file: " << trace_file
                        << std::endl;
        exit(1);
      }
    }
    Request next() {
      std::unordered_set<std::string> write_types = {"WRITE", "write",
                                                     "P_MEM_WR", "BOFF"};

      Addr addr;
      Tick tick;
      std::string op;
      trace_file >> std::hex >> addr >> op >> std::dec >> tick;
      bool is_write = write_types.count(op) == 1;
      auto ep = end_points[cur].id;
      cur = (cur + 1) % end_points.size();
      return {ep, addr, tick, is_write};
    }
  };

  class Stream : public Interleaving {
  public:
    Stream(size_t block_size = 64) : Interleaving(block_size) {}
    Request next() {
      auto ep = end_points[cur].id;
      auto addr = end_points[cur].cur;
      end_points[cur].cur += block_size;
      if (end_points[cur].cur >=
          end_points[cur].start + end_points[cur].capacity)
        end_points[cur].cur = end_points[cur].start;
      bool is_write = uni(gen) < end_points[cur].ratio;
      cur = (cur + 1) % end_points.size();
      return {ep, addr, 0, is_write};
    }
  };

  class Random : public Interleaving {
  private:
    std::normal_distribution<> norm;

  public:
    Random(size_t block_size = 64) : Interleaving(block_size) {
      norm = std::normal_distribution<>(0.5, 0.5);
    }
    Request next() {
      auto ep = end_points[cur].id;
      auto seed = fmax(0, norm(gen));
      seed = fmin(seed, 1);
      auto addr =
          Addr(floor(double(end_points[cur].capacity) / block_size * seed)) *
              block_size +
          end_points[cur].start;
      bool is_write = uni(gen) < end_points[cur].ratio;
      cur = (cur + 1) % end_points.size();
      return {ep, addr, 0, is_write};
    }
  };
};
} // namespace xerxes
