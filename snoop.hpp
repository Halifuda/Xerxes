#pragma once
#include "def.hpp"
#include "device.hpp"
#include "utils.hpp"

#include <map>
#include <unordered_set>
#include <utility>

namespace xerxes {
class Snoop : public Device {
public:
  class SnoopEviction {
  protected:
    size_t size;
    size_t assoc;
    size_t setn;

  public:
    SnoopEviction() {}
    virtual ~SnoopEviction() {}
    virtual void init(size_t size, size_t assoc) {
      this->size = size;
      this->assoc = assoc;
      this->setn = size / assoc;
    }
    // Defaultly we do nothing.
    virtual void on_hit(Addr addr, size_t set_i, size_t way_i) {}
    virtual void on_update(Addr addr, size_t set_i, size_t way_i) {}
    virtual void on_insert(Addr addr, size_t set_i, size_t way_i) {}
    virtual void on_invalidate(Addr addr, size_t set_i, size_t way_i) {}
    virtual ssize_t find_victim(size_t set_i, bool do_evict) = 0;
  };

  class FIFO;
  class LIFO;
  class LRU;
  class MRU;
  class Random;

private:
  size_t line_num;
  size_t assoc;
  size_t set_num;

  SnoopEviction *eviction;
  bool log_inv = false;

  enum State {
    EXCLUSIVE,
    WAIT_DRAM,
    EVICTING,
    INVALID,
  };

  struct Line {
    Addr addr;
    TopoID owner;
    State state;
    bool valid;
  };

  std::vector<std::vector<Line>> cache;
  std::vector<std::map<PktID, Packet>> waiting;

  std::unordered_map<TopoID, double> host_trig_conflict_count;

  size_t set_of(Addr addr) { return (addr / 64) % set_num; }

  ssize_t hit(Addr addr, TopoID owner) {
    auto set_i = set_of(addr);
    auto &set = cache[set_i];
    for (ssize_t i = 0; i < (ssize_t)assoc; ++i) {
      auto &line = set[i];
      if (line.valid && line.addr == addr && line.owner == owner) {
        if (eviction)
          eviction->on_hit(addr, set_i, i);
        return i;
      }
    }
    return -1;
  }

  ssize_t new_way(Addr addr) {
    auto &set = cache[set_of(addr)];
    for (ssize_t i = 0; i < (ssize_t)assoc; ++i) {
      auto &line = set[i];
      if (!line.valid)
        return i;
    }
    return -1;
  }

  void update(Addr addr, size_t set_i, size_t way_i, TopoID owner, State state,
              bool valid, bool update_evict = true) {
    bool is_insert = !cache[set_i][way_i].valid && valid;
    bool is_invalidate = cache[set_i][way_i].valid && !valid;
    auto &line = cache[set_i][way_i];
    line.addr = addr;
    line.owner = owner;
    line.state = state;
    line.valid = valid;
    if (update_evict && eviction) {
      if (is_insert)
        eviction->on_insert(addr, set_i, way_i);
      else if (is_invalidate)
        eviction->on_invalidate(addr, set_i, way_i);
      else
        eviction->on_update(addr, set_i, way_i);
    }
  }

  void evict(size_t set_i, Tick tick) {
    ASSERT(eviction != nullptr, name() + ": eviction policy is null");
    auto &set = cache[set_i];
    auto victim = eviction->find_victim(set_i, true);
    Logger::debug() << name() << ": evict victim [" << set_i << ": " << victim
                    << "]" << std::endl;
    if (victim != -1) {
      auto &line = set[victim];
      auto inv = PktBuilder()
                     .type(PacketType::INV)
                     .addr(line.addr)
                     .payload(64)
                     .sent(tick)
                     .arrive(0)
                     .src(self)
                     .dst(line.owner)
                     .is_rsp(false)
                     .build();
      Logger::debug() << name() << ": evict packet " << inv.id << " [" << set_i
                      << ": " << victim << "] addr " << line.addr << " owner "
                      << line.owner << std::endl;
      send_pkt(inv);
      line.state = EVICTING;
    } else {
      // No victim, do nothing.
    }
  }

  void filter(Packet pkt) {
    if (pkt.is_coherent() && !pkt.is_rsp) {
      // Coherence packet. Need to record in snoop cache.
      auto set_i = set_of(pkt.addr);
      auto way_i = hit(pkt.addr, pkt.src);
      if (way_i == -1) {
        // Not hit. Need to allocate a new way.
        auto new_way_i = new_way(pkt.addr);
        if (new_way_i == -1) {
          // No empty way. Need to evict. Packet need to wait until evict done.
          if (host_trig_conflict_count.find(pkt.src) ==
              host_trig_conflict_count.end()) {
            host_trig_conflict_count[pkt.src] = 0;
          }
          host_trig_conflict_count[pkt.src] += 1;
          Logger::debug() << name() << ": pkt " << pkt.id << " wait evict ["
                          << set_i << "]" << std::endl;
          waiting[set_i].insert(std::make_pair(pkt.id, pkt));
          evict(set_i, pkt.arrive);
        } else {
          // Empty way. Allocate.
          Logger::debug() << name() << ": pkt " << pkt.id << " allocate ["
                          << set_i << ":" << new_way_i << "]" << std::endl;
          update(pkt.addr, set_i, new_way_i, pkt.src, WAIT_DRAM, true, false);

          // Directly send the packet.
          send_pkt(pkt);
        }
      } else {
        Logger::debug() << name() << ": pkt " << pkt.id << " hit at [" << set_i
                        << ":" << way_i << "]" << std::endl;
        // Hit. Directly send back (host should hold the data already).
        std::swap(pkt.src, pkt.dst);
        pkt.is_rsp = true;
        send_pkt(pkt);
      }
    } else if (pkt.type == PacketType::INV && pkt.is_rsp && pkt.dst == self) {
      // INV response.
      if (log_inv)
        pkt.log_stat();
      // Update snoop cache.
      auto tick = pkt.arrive;
      auto set_i = set_of(pkt.addr);
      auto way_i = hit(pkt.addr, pkt.src);
      // Send the write back packet.
      // TODO: issue delay
      pkt.type = PacketType::NT_WT;
      pkt.is_rsp = false;
      send_pkt(pkt);
      if (way_i != -1) {
        // Invalidate the line.
        update(0, set_i, way_i, -1, INVALID, false);
      }
      // Check waiting.
      auto waiting_it = waiting[set_i].begin();
      if (waiting_it != waiting[set_i].end()) {
        // Some packet is waiting for this eviction.
        auto &waiter = waiting_it->second;
        Logger::debug() << name() << ": insert waiter pkt " << waiter.id
                        << " to [" << set_i << ":" << way_i << "]" << std::endl;
        update(waiter.addr, set_i, way_i, waiter.src, WAIT_DRAM, true, false);
        // Send the waiting.
        if (tick > waiter.arrive) {
          waiter.delta_stat(SNOOP_EVICT_DELAY, (double)(tick - waiter.arrive));
          waiter.arrive = tick;
        }
        send_pkt(waiting_it->second);
        waiting[set_i].erase(waiting_it);
      }
    } else {
      // Non-temporal or response. Directly send the packet.
      if (pkt.is_rsp) {
        auto tick = pkt.arrive;
        auto set_i = set_of(pkt.addr);
        auto way_i = hit(pkt.addr, pkt.dst);
        if (way_i != -1) {
          Logger::debug() << name() << ": DRAM rsp pkt " << pkt.id << " hit ["
                          << set_i << ":" << way_i << "]" << std::endl;
          update(pkt.addr, set_i, way_i, pkt.dst, EXCLUSIVE, true, true);
          if (waiting[set_i].size() > 0) {
            // Try an eviction.
            Logger::debug() << " try evict [" << set_i << "]" << std::endl;
            evict(set_i, tick);
          }
        }
      }
      Logger::debug() << name() << " send packet " << pkt.id << std::endl;
      log_transit_normal(pkt);
      send_pkt(pkt);
    }
  }

public:
  Snoop(Topology *topology, size_t line_num, size_t assoc,
        SnoopEviction *eviction = nullptr, bool log_inv = false,
        std::string name = "Snoop")
      : Device(topology, name), line_num(line_num), assoc(assoc),
        set_num(line_num / assoc), eviction(eviction), log_inv(log_inv) {
    ASSERT(line_num % assoc == 0, "snoop: size % assoc != 0");
    cache.resize(set_num);
    for (auto &c : cache)
      c.resize(assoc, Line{0, false});
    waiting.resize(set_num);
    if (eviction)
      eviction->init(this->line_num, assoc);
  }

  ~Snoop() {}

  void transit() override {
    // filter all packets
    auto pkt = receive_pkt();
    if (!pkt.is_rsp)
      Logger::debug() << name() << " receive packet " << pkt.id << std::endl;
    filter(pkt);
  }

  void log_stats(std::ostream &os) override {
    os << name() << " stats:" << std::endl;
    for (auto &pair : host_trig_conflict_count) {
      auto &host = pair.first;
      auto &count = pair.second;
      os << " - host " << host << " conflict count: " << count << std::endl;
    }
  }

  // TODO: TEMP
  double avg_conflict_cnt() {
    double sum = 0;
    for (auto &pair : host_trig_conflict_count) {
      sum += pair.second;
    }
    return sum / host_trig_conflict_count.size();
  }

  class FIFO : public SnoopEviction {
  protected:
    std::vector<std::list<size_t>> queues;

  public:
    FIFO() : SnoopEviction() {}

    void init(size_t size, size_t assoc) override {
      SnoopEviction::init(size, assoc);
      queues.resize(setn, std::list<size_t>{});
    }

    void on_invalidate(Addr addr, size_t set_i, size_t way_i) override {
      auto &q = queues[set_i];
      for (auto it = q.begin(); it != q.end(); ++it) {
        if (*it == way_i) {
          q.erase(it);
          return;
        }
      }
    }

    void on_insert(Addr addr, size_t set_i, size_t way_i) override {
      bool exist = false;
      for (auto &way : queues[set_i]) {
        if (way == way_i) {
          exist = true;
          break;
        }
      }
      if (!exist) {
        if (queues[set_i].size() == assoc) {
          queues[set_i].pop_back();
        }
        queues[set_i].push_front(way_i);
      }
    }

    void on_update(Addr addr, size_t set_i, size_t way_i) override {
      auto &q = queues[set_i];
      for (auto it = q.begin(); it != q.end(); ++it) {
        if (*it == way_i) {
          q.erase(it);
          break;
        }
      }
      q.push_front(way_i);
    }

    ssize_t find_victim(size_t set_i, bool do_evict) override {
      if (queues[set_i].empty())
        return -1;
      auto victim = queues[set_i].back();
      if (do_evict)
        queues[set_i].pop_back();
      return victim;
    }
  };

  class LIFO : public FIFO {
  public:
    LIFO() : FIFO() {}
    /*
      on_insert() and on_update() are the same as FIFO.
      The front of the queue is the last inserted way.
    */
    ssize_t find_victim(size_t set_i, bool do_evict) override {
      if (queues[set_i].empty())
        return -1;
      auto victim = queues[set_i].front();
      if (do_evict)
        queues[set_i].pop_front();
      return victim;
    }
  };

  class LRU : public FIFO {
  public:
    LRU() : FIFO() {}

    /*
      When update queue on hit, the front of the queue is the most recently
      used way, and the back of the queue is the least recently used way.
    */

    void on_hit(Addr addr, size_t set_i, size_t way_i) override {
      auto &q = queues[set_i];
      for (auto it = q.begin(); it != q.end(); ++it) {
        if (*it == way_i) {
          q.erase(it);
          q.push_front(way_i);
          return;
        }
      }
    }

    void on_insert(Addr addr, size_t set_i, size_t way_i) override {
      auto &q = queues[set_i];
      for (auto it = q.begin(); it != q.end(); ++it) {
        if (*it == way_i) {
          q.erase(it);
          break;
        }
      }
      if (queues[set_i].size() == assoc) {
        queues[set_i].pop_back();
      }
      queues[set_i].push_front(way_i);
    }
  };

  class MRU : public LRU {
  public:
    MRU() : LRU() {}

    ssize_t find_victim(size_t set_i, bool do_evict) override {
      if (queues[set_i].empty())
        return -1;
      auto victim = queues[set_i].front();
      if (do_evict)
        queues[set_i].pop_front();
      return victim;
    }
  };

  class Random : public SnoopEviction {
  protected:
    std::vector<std::unordered_set<size_t>> queues;
    size_t seed = 19260817;

  public:
    Random() : SnoopEviction() {}

    void init(size_t size, size_t assoc) override {
      SnoopEviction::init(size, assoc);
      queues.resize(setn, std::unordered_set<size_t>{});
    }

    void on_invalidate(Addr addr, size_t set_i, size_t way_i) override {
      queues[set_i].erase(way_i * seed);
    }

    void on_insert(Addr addr, size_t set_i, size_t way_i) override {
      way_i *= seed;
      if (queues[set_i].find(way_i) == queues[set_i].end()) {
        if (queues[set_i].size() == assoc) {
          auto it = queues[set_i].begin();
          queues[set_i].erase(it);
        }
        queues[set_i].insert(way_i);
      }
    }

    ssize_t find_victim(size_t set_i, bool do_evict) override {
      if (queues[set_i].empty())
        return -1;
      auto it = queues[set_i].begin();
      auto victim = *it;
      if (do_evict)
        queues[set_i].erase(it);
      return victim;
    }
  };
};

} // namespace xerxes
