#pragma once
#ifndef XERXES_REQUESTER_HH
#define XERXES_REQUESTER_HH

#include "device.hh"
#include "utils.hh"

#include <algorithm>
#include <cmath>
#include <fstream>
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
    virtual bool eof() = 0;
  };

private:
  class FakeLRUCache {
    std::list<Addr> cache;

  public:
    size_t capacity;
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
    size_t size() { return queue.size(); }
    size_t cap() { return capacity; }
    void push(const Packet &pkt) {
      if (full()) {
        XerxesLogger::warning() << "Queue is full!" << std::endl;
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
  size_t cur_cnt = 0;
  Tick issue_delay;
  bool coherent;
  size_t burst_size = 1;
  size_t block_size = 64;

  std::unordered_map<TopoID, std::unordered_map<std::string, double>> stats;

public:
  Requester(Simulation *sim, size_t q_capacity, size_t cache_capacity,
            Tick cache_delay, Tick issue_delay, bool coherent,
            size_t burst_size = 1, Interleaving *interleave = nullptr,
            std::string name = "Host")
      : Device(sim, name), end_points(interleave), q(q_capacity),
        cache(cache_capacity, cache_delay), issue_delay(issue_delay),
        coherent(coherent), burst_size(burst_size),
        block_size(burst_size * 64) {
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
        XerxesLogger::debug()
            << name() << " receive packet " << pkt.id
            << ", issue queue is full? " << q.full() << std::endl;
        last_arrive = pkt.arrive;
        cache.insert(pkt.addr);

        // Update stats
        stats[pkt.src]["Count"] += 1;
        stats[pkt.src]["Bandwidth"] += pkt.burst * 64;
        stats[pkt.src]["Average latency"] += pkt.arrive - pkt.sent;
        stats[pkt.src]["Average wait for evict"] +=
            pkt.get_stat(SNOOP_EVICT_DELAY);

        if (q.full())
          register_issue_event(pkt.arrive);
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

  double get_agg_stat(std::string name) {
    double sum = 0;
    double cnt = 0;
    if (name == "Bandwidth") {
      for (auto &pair : stats) {
        sum += pair.second[name] * 1000 / (double)(last_arrive);
      }
    } else if (name.find("Average") != std::string::npos) {
      for (auto &pair : stats) {
        sum += pair.second[name];
        cnt += pair.second["Count"];
      }
      sum /= cnt;
    } else if (name == "Cache hit count" || name == "Cache evict count") {
      sum = stats[-1][name];
    } else {
      for (auto &pair : stats) {
        sum += pair.second[name];
      }
    }
    return sum;
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
    static bool ended = false;
    if (!end_points->eof()) {
      if (q.full()) {
        if (cur < last_arrive)
          cur = last_arrive;
        return false;
      }
      auto req = end_points->next();
      auto ep = req.id;
      auto addr = req.addr;
      cur += issue_delay;
      // TODO: Strict to trace?
      if (req.tick != 0)
        cur = req.tick;
      if (cache.hit(addr)) {
        stats[ep]["Count"] += 1;
        stats[ep]["Bandwidth"] += burst_size * 64;
        stats[ep]["Average latency"] += cache.delay;
        stats[-1]["Cache hit count"] += 1;

        XerxesLogger::info() << "Cache hit: " << addr << "," << cur << ","
                             << cur + cache.delay << std::endl;
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
    } else {
      if (!ended) {
        ended = true;
        for (auto &ep : end_points->end_points) {
          auto pkt = PktBuilder()
                         .src(self)
                         .dst(ep.id)
                         .addr(0)
                         .sent(cur)
                         .payload(0)
                         .burst(0)
                         .type(PacketType::NT_RD)
                         .build();
          q.push(pkt);
          send_pkt(pkt);
        }
        return true;
      }
    }
    return false;
  }

  void issue_event() {
    if (step(coherent)) {
      register_issue_event(cur);
    }
  }

  void register_issue_event(Tick tick) {
    xerxes_schedule([this]() { this->issue_event(); }, tick);
  }

  bool all_issued() { return end_points->eof(); }
  bool q_empty() { return q.empty(); }

  /* ------------------------------ Access Type ----------------------------- */

  class Trace : public Interleaving {
  public:
    struct TraceReq {
      Addr addr;
      bool is_write;
      Tick tick;
    };

  private:
    std::ifstream trace_file;
    std::function<TraceReq(std::ifstream &)> decoder;

  public:
    Trace(
        std::string trace_file,
        std::function<TraceReq(std::ifstream &)> decoder =
            [](std::ifstream &file) {
              static std::unordered_set<std::string> write_types = {
                  "W", "WR", "WRITE", "write", "P_MEM_WR", "BOFF"};
              std::string type;
              Addr addr;
              Tick tick;
              file >> std::hex >> addr >> std::dec >> type >> tick;
              return TraceReq{addr, write_types.count(type) > 0, tick};
            },
        size_t block_size = 64)
        : Interleaving(block_size), decoder(decoder) {
      this->trace_file.open(trace_file);
      if (!this->trace_file.is_open()) {
        XerxesLogger::error()
            << "Cannot open trace file: " << trace_file << std::endl;
        exit(1);
      }
    }
    bool eof() { return trace_file.eof(); }
    Request next() {
      // TODO: flexible trace decoding
      auto req = decoder(trace_file);
      auto ep = end_points[cur].id;
      req.addr = (req.addr % end_points[cur].capacity) + end_points[cur].start;
      cur = (cur + 1) % end_points.size();
      return {ep, req.addr, req.tick, req.is_write};
    }
  };

  class Stream : public Interleaving {
    size_t total_count;
    size_t cur_count = 0;

  public:
    Stream(size_t total_count, size_t block_size = 64)
        : Interleaving(block_size), total_count(total_count) {}
    bool eof() { return cur_count == total_count; }
    Request next() {
      auto ep = end_points[cur].id;
      auto addr = end_points[cur].cur;
      end_points[cur].cur += block_size;
      if (end_points[cur].cur >=
          end_points[cur].start + end_points[cur].capacity)
        end_points[cur].cur = end_points[cur].start;
      bool is_write = uni(gen) < end_points[cur].ratio;
      cur = (cur + 1) % end_points.size();
      cur_count++;
      return {ep, addr, 0, is_write};
    }
  };

  class Random : public Interleaving {
    size_t total_count;
    size_t cur_count = 0;
    std::normal_distribution<> norm;

  public:
    Random(size_t total_count, size_t block_size = 64)
        : Interleaving(block_size), total_count(total_count) {
      norm = std::normal_distribution<>(0.5, 0.5);
    }
    bool eof() { return cur_count == total_count; }
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
      cur_count++;
      return {ep, addr, 0, is_write};
    }
  };
};

class RequesterBuilder {
private:
  Simulation *sim_i;
  size_t q_capacity_i;
  size_t cache_capacity_i;
  Tick cache_delay_i;
  Tick issue_delay_i;
  bool coherent_i;
  size_t burst_size_i;
  Requester::Interleaving *interleave_i;
  std::string name_i;

public:
  RequesterBuilder()
      : sim_i(nullptr), q_capacity_i(0), cache_capacity_i(0), cache_delay_i(0),
        issue_delay_i(0), coherent_i(false), burst_size_i(1),
        interleave_i(nullptr), name_i("Host") {}

  RequesterBuilder &simulation(Simulation *sim) {
    this->sim_i = sim;
    return *this;
  }
  RequesterBuilder &q_capacity(size_t q_capacity) {
    this->q_capacity_i = q_capacity;
    return *this;
  }
  RequesterBuilder &cache_capacity(size_t cache_capacity) {
    this->cache_capacity_i = cache_capacity;
    return *this;
  }
  RequesterBuilder &cache_delay(Tick cache_delay) {
    this->cache_delay_i = cache_delay;
    return *this;
  }
  RequesterBuilder &issue_delay(Tick issue_delay) {
    this->issue_delay_i = issue_delay;
    return *this;
  }
  RequesterBuilder &coherent(bool coherent) {
    this->coherent_i = coherent;
    return *this;
  }
  RequesterBuilder &burst_size(size_t burst_size) {
    this->burst_size_i = burst_size;
    return *this;
  }
  RequesterBuilder &interleave(Requester::Interleaving *interleave) {
    this->interleave_i = interleave;
    return *this;
  }
  RequesterBuilder &name(std::string name) {
    this->name_i = name;
    return *this;
  }
  Requester build() {
    return Requester(sim_i, q_capacity_i, cache_capacity_i, cache_delay_i,
                     issue_delay_i, coherent_i, burst_size_i, interleave_i,
                     name_i);
  }
};

} // namespace xerxes

#endif // XERXES_REQUESTER_HH
