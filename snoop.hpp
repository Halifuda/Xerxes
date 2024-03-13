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
    virtual ssize_t find_victim(size_t set_i, bool do_evict) = 0;
  };

  class FIFO : public SnoopEviction {
  private:
    std::vector<std::deque<size_t>> fifo;

  public:
    FIFO() : SnoopEviction() {}
    void init(size_t size, size_t assoc) override {
      SnoopEviction::init(size, assoc);
      fifo.resize(setn, std::deque<size_t>());
    }
    void on_insert(Addr addr, size_t set_i, size_t way_i) override {
      if (fifo[set_i].size() == assoc)
        fifo[set_i].pop_back();
      fifo[set_i].push_front(way_i);
    }
    void on_update(Addr addr, size_t set_i, size_t way_i) override {
      auto &q = fifo[set_i];
      for (auto it = q.begin(); it != q.end(); ++it) {
        if (*it == way_i) {
          q.erase(it);
          break;
        }
      }
      q.push_front(way_i);
    }
    ssize_t find_victim(size_t set_i, bool do_evict) override {
      if (fifo[set_i].empty())
        return -1;
      auto victim = fifo[set_i].front();
      if (do_evict)
        fifo[set_i].pop_front();
      return victim;
    }
  };

private:
  size_t line_num;
  size_t assoc;
  size_t set_num;

  SnoopEviction *eviction;

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

  ssize_t hit(Addr addr) {
    auto &set = cache[addr % set_num];
    for (ssize_t i = 0; i < (ssize_t)assoc; ++i) {
      auto &line = set[i];
      if (line.valid && line.addr == addr) {
        if (eviction)
          eviction->on_hit(addr, addr % set_num, i);
        return i;
      }
    }
    return -1;
  }

  ssize_t new_way(Addr addr) {
    auto &set = cache[addr % set_num];
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
    ASSERT(eviction != nullptr, name_ + ": eviction policy is null");
    auto &set = cache[set_i];
    auto victim = eviction->find_victim(set_i, true);
    Logger::debug() << "evict [" << set_i << "] victim " << victim << std::endl;
    if (victim != -1) {
      auto &line = set[victim];
      auto inv = PktBuilder()
                     .type(PacketType::INV)
                     .addr(line.addr)
                     .payload(256)
                     .sent(tick)
                     .arrive(0)
                     .src(self)
                     .dst(line.owner)
                     .is_rsp(false)
                     .build();
      send_pkt(inv);
      line.state = EVICTING;
    } else {
      // No victim, do nothing.
    }
  }

  void filter(Packet pkt) {
    Logger::debug() << name_ << " snooping packet: " << pkt.id
                    << (pkt.is_rsp ? " (rsp)" : "") << std::endl;
    if (pkt.is_coherent() && !pkt.is_rsp) {
      // Coherence packet. Need to record in snoop cache.
      auto set_i = pkt.addr % set_num;
      auto way_i = hit(pkt.addr);
      if (way_i == -1) {
        // Not hit. Need to allocate a new way.
        auto new_way_i = new_way(pkt.addr);
        if (new_way_i == -1) {
          // No empty way. Need to evict. Packet need to wait until evict done.
          Logger::debug() << "pkt " << pkt.id << "@" << pkt.addr
                          << " wait evict [" << set_i << "]" << std::endl;
          waiting[set_i].insert(std::make_pair(pkt.id, pkt));
          evict(set_i, pkt.arrive);
        } else {
          // Empty way. Allocate.
          Logger::debug() << "pkt " << pkt.id << "@" << pkt.addr
                          << " allocate [" << set_i << ":" << new_way_i << "]"
                          << std::endl;
          update(pkt.addr, set_i, new_way_i, pkt.src, WAIT_DRAM, true, false);

          // Directly send the packet.
          send_pkt(pkt);
        }
      }
      // Hit. Ignore (host should hold the data already).
    } else if (pkt.type == PacketType::INV && pkt.is_rsp && pkt.dst == self) {
      // INV response.
      // pkt.log_stat();
      // Update snoop cache.
      auto tick = pkt.arrive;
      auto set_i = pkt.addr % set_num;
      auto way_i = hit(pkt.addr);
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
        update(waiter.addr, set_i, way_i, waiter.src, WAIT_DRAM, true, false);
        // Send the waiting.
        waiter.delta_stat(SNOOP_EVICT_DELAY, (double)(tick - waiter.arrive));
        waiter.arrive = tick;
        send_pkt(waiting_it->second);
        waiting[set_i].erase(waiting_it);
      }
    } else {
      // Non-temporal or response. Directly send the packet.
      if (pkt.is_rsp) {
        auto tick = pkt.arrive;
        auto set_i = pkt.addr % set_num;
        auto way_i = hit(pkt.addr);
        if (way_i != -1) {
          Logger::debug() << "DRAM rsp: pkt " << pkt.id << "@" << pkt.addr
                          << " hit [" << set_i << ":" << way_i << "]"
                          << std::endl;
          update(pkt.addr, set_i, way_i, pkt.dst, EXCLUSIVE, true, true);
          if (waiting[set_i].size() > 0) {
            // Try an eviction.
            Logger::debug() << " try evict [" << set_i << "]" << std::endl;
            evict(set_i, tick);
          }
        }
      }
      log_transit_normal(pkt);
      send_pkt(pkt);
    }
  }

public:
  Snoop(Topology *topology, size_t line_num, size_t assoc,
        SnoopEviction *eviction = nullptr, std::string name = "Snoop")
      : Device(topology, name), line_num(line_num), assoc(assoc),
        set_num(line_num / assoc), eviction(eviction) {
    ASSERT(line_num % assoc == 0, "snoop: size % assoc != 0");
    cache.resize(set_num);
    for (auto &c : cache)
      c.resize(assoc, Line{0, false});
    waiting.resize(set_num);
    if (eviction)
      eviction->init(this->line_num, assoc);
  }

  ~Snoop() {
    if (eviction)
      delete eviction;
  }

  void transit() override {
    // filter all packets
    auto pkt = receive_pkt();
    filter(pkt);
  }
};

} // namespace xerxes
