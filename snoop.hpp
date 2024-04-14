#pragma once
#include "def.hpp"
#include "device.hpp"
#include "utils.hpp"

#include <map>
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
    virtual void on_evict(Addr addr, size_t set_i, size_t way_i) {}
    virtual ssize_t find_victim(size_t set_i, bool do_evict) = 0;
  };

  class FIFO;
  class LIFO;
  class LRU;
  class MRU;
  class LFI;

private:
  size_t line_num;
  size_t assoc;
  size_t set_num;
  size_t max_burst_inv;

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
  std::unordered_map<size_t, double> burst_inv_size_count;
  std::unordered_map<Addr, size_t> evict_count;

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

  // TODO: hard-coded block size 64
  std::pair<Addr, size_t> peek_burst_evict(Addr addr, TopoID owner) {
    size_t burst = 1;
    auto begin_addr = addr;
    auto end_addr = addr;
    bool flag = true;
    while (flag && begin_addr <= addr &&
           addr - begin_addr < max_burst_inv * 64) {
      auto way = hit(begin_addr - 64, owner);
      if (way != -1) {
        begin_addr -= 64;
        burst += 1;
      } else {
        flag = false;
      }
    }
    flag = true;
    while (flag && end_addr >= addr && end_addr - addr < max_burst_inv * 64) {
      auto way = hit(end_addr + 64, owner);
      if (way != -1) {
        end_addr += 64;
        burst += 1;
      } else {
        flag = false;
      }
    }
    return {begin_addr, burst};
  }

  void conduct_burst_evict(Addr start, size_t burst, TopoID owner, Tick tick) {
    for (size_t i = 0; i < burst; ++i) {
      auto way = hit(start + i * 64, owner);
      if (way != -1) {
        auto &line = cache[set_of(start + i * 64)][way];
        if (evict_count.find(line.addr) == evict_count.end()) {
          evict_count[line.addr] = 0;
        }
        evict_count[line.addr] += 1;
        line.state = EVICTING;
        if (eviction != nullptr)
          eviction->on_evict(line.addr, set_of(start + i * 64), way);
      }
    }
    auto inv = PktBuilder()
                   .type(PacketType::INV)
                   .addr(start)
                   .payload(0)
                   .burst(burst)
                   .sent(tick)
                   .arrive(0)
                   .src(self)
                   .dst(owner)
                   .is_rsp(false)
                   .build();
    Logger::debug() << name() << ": evict packet " << inv.id << ", addr "
                    << start << ", burst " << burst << ", owner " << owner
                    << std::endl;
    send_pkt(inv);
  }

  void evict(size_t set_i, Tick tick) {
    ASSERT(eviction != nullptr, name() + ": eviction policy is null");
    auto &set = cache[set_i];
    auto victim = eviction->find_victim(set_i, true);
    Logger::debug() << name() << ": evict victim [" << set_i << ": " << victim
                    << "]" << std::endl;
    if (victim != -1) {
      auto &line = set[victim];
      auto peek = peek_burst_evict(line.addr, line.owner);

      if (burst_inv_size_count.find(peek.second) ==
          burst_inv_size_count.end()) {
        burst_inv_size_count[peek.second] = 0;
      }
      burst_inv_size_count[peek.second] += 1;

      conduct_burst_evict(peek.first, peek.second, line.owner, tick);
    } else {
      // No victim, do nothing.
    }
  }

  void coherent_request(Packet pkt) {
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
        update(pkt.addr, set_i, new_way_i, pkt.src, WAIT_DRAM, true, true);

        // Directly send the packet.
        send_pkt(pkt);
      }
    } else {
      auto &line = cache[set_i][way_i];
      if (line.owner != pkt.src) {
        // Conflict. Need to evict the line.
        auto peek = peek_burst_evict(line.addr, line.owner);
        conduct_burst_evict(peek.first, peek.second, line.owner, pkt.arrive);
        // Insert the packet to waiting list.
        Logger::debug() << name() << ": pkt " << pkt.id << " conflict ["
                        << set_i << ":" << way_i << "]" << std::endl;
        waiting[set_i].insert(std::make_pair(pkt.id, pkt));
      } else {
        // Hit. Directly send back (host should hold the data already).
        Logger::debug() << name() << ": pkt " << pkt.id << " hit at [" << set_i
                        << ":" << way_i << "]" << std::endl;
        std::swap(pkt.src, pkt.dst);
        pkt.is_rsp = true;
        send_pkt(pkt);
      }
    }
  }

  void invalidate_response(Packet pkt) {
    // INV response.
    if (log_inv)
      pkt.log_stat();
    // Update snoop cache.
    auto tick = pkt.arrive;
    auto addr = pkt.addr;
    auto burst = pkt.burst;
    for (size_t i = 0; i < burst; ++i) {
      auto set_i = set_of(addr + i * 64);
      auto way_i = hit(addr + i * 64, pkt.src);
      // TODO: Send the write back packet.
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
        update(waiter.addr, set_i, way_i, waiter.src, WAIT_DRAM, true, true);
        // Send the waiting.
        if (tick > waiter.arrive) {
          waiter.delta_stat(SNOOP_EVICT_DELAY, (double)(tick - waiter.arrive));
          waiter.arrive = tick;
        }
        send_pkt(waiting_it->second);
        waiting[set_i].erase(waiting_it);
      }
    }
  }

  void filter(Packet pkt) {
    if (pkt.is_coherent() && !pkt.is_rsp) {
      coherent_request(pkt);
    } else if (pkt.type == PacketType::INV && pkt.is_rsp && pkt.dst == self) {
      invalidate_response(pkt);
    } else {
      // Non-temporal or response. Directly send the packet.
      if (pkt.is_rsp) {
        auto tick = pkt.arrive;
        auto set_i = set_of(pkt.addr);
        auto way_i = hit(pkt.addr, pkt.dst);
        if (way_i != -1) {
          Logger::debug() << name() << ": DRAM rsp pkt " << pkt.id << " hit ["
                          << set_i << ":" << way_i << "]" << std::endl;
          update(pkt.addr, set_i, way_i, pkt.dst, EXCLUSIVE, true, false);
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
  Snoop(Topology *topology, size_t line_num, size_t assoc, size_t max_burst_inv,
        SnoopEviction *eviction = nullptr, bool log_inv = false,
        std::string name = "Snoop")
      : Device(topology, name), line_num(line_num), assoc(assoc),
        set_num(line_num / assoc), max_burst_inv(max_burst_inv),
        eviction(eviction), log_inv(log_inv) {
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
      os << " * host " << host << " conflict count: " << count << std::endl;
    }
    double avg_burst_inv = 0;
    double total_burst_inv = 0;
    for (auto &pair : burst_inv_size_count) {
      auto &burst = pair.first;
      auto &count = pair.second;
      avg_burst_inv += burst * count;
      total_burst_inv += count;
    }
    avg_burst_inv /= total_burst_inv;
    os << " * average burst invalidation size: " << avg_burst_inv << std::endl;

    size_t evict_count_pdf[11] = {0}; // x[i] means count >= 2^i and < 2^(i+1)
    for (auto &pair : evict_count) {
      auto &count = pair.second;
      size_t i = 0;
      while (count >= ((size_t)1 << i) && i < 10) {
        i += 1;
      }
      evict_count_pdf[i] += 1;
    }
    os << " * Evict count distribution: " << std::endl;
    for (size_t i = 0; i < 11; ++i) {
      os << "   - [" << (1 << i) << ", " << (1 << (i + 1))
         << "): " << evict_count_pdf[i] << std::endl;
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

  class LFI : public SnoopEviction {
  protected:
    struct Entry {
      Addr addr;
      size_t way_i;
    };
    std::vector<std::list<Entry>> queues;
    std::unordered_map<Addr, size_t> insert_cnt;

  public:
    LFI() : SnoopEviction() {}

    void init(size_t size, size_t assoc) override {
      SnoopEviction::init(size, assoc);
      queues.resize(setn, std::list<Entry>{});
    }

    void on_invalidate(Addr addr, size_t set_i, size_t way_i) override {
      auto &q = queues[set_i];
      for (auto it = q.begin(); it != q.end(); ++it) {
        if (it->way_i == way_i) {
          q.erase(it);
          return;
        }
      }
    }

    void on_insert(Addr addr, size_t set_i, size_t way_i) override {
      if (insert_cnt.find(addr) == insert_cnt.end()) {
        insert_cnt[addr] = 0;
      }
      insert_cnt[addr] += 1;
      bool exist = false;
      for (auto &e : queues[set_i]) {
        if (e.way_i == way_i) {
          exist = true;
          break;
        }
      }
      if (!exist) {
        if (queues[set_i].size() == assoc) {
          queues[set_i].pop_back();
        }
        queues[set_i].push_front({addr, way_i});
      }
    }

    void on_update(Addr addr, size_t set_i, size_t way_i) override {
      if (insert_cnt.find(addr) == insert_cnt.end()) {
        insert_cnt[addr] = 0;
      }
      insert_cnt[addr] += 1;
      auto &q = queues[set_i];
      for (auto it = q.begin(); it != q.end(); ++it) {
        if (it->way_i == way_i) {
          q.erase(it);
          break;
        }
      }
      q.push_front({addr, way_i});
    }

    ssize_t find_victim(size_t set_i, bool do_evict) override {
      if (queues[set_i].empty())
        return -1;
      size_t ecnt = INT_MAX;
      size_t victim = -1;
      for (auto &e : queues[set_i]) {
        size_t k = 0;
        if (insert_cnt.find(e.addr) != insert_cnt.end())
          k = insert_cnt[e.addr];
        if (k < ecnt) {
          ecnt = k;
          victim = e.way_i;
        }
      }
      if (do_evict) {
        auto &q = queues[set_i];
        for (auto it = q.begin(); it != q.end(); ++it) {
          if (it->way_i == victim) {
            q.erase(it);
            break;
          }
        }
      }
      return victim;
    }
  };
};

} // namespace xerxes
