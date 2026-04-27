#pragma once
#ifndef XERXES_SNOOP_HH
#define XERXES_SNOOP_HH

#include "device.hh"
#include "event_log.hh"
#include "utils.hh"

#include <algorithm>
#include <map>
#include <random>
#include <set>
#include <utility>

namespace xerxes {
class SnoopConfig {
  public:
    size_t line_num = 1024;
    size_t assoc = 8;
    size_t max_burst_inv = 8;
    std::vector<std::pair<Addr, Addr>> ranges;
    std::string eviction = "LRU";
};
} // namespace xerxes

TOML11_DEFINE_CONVERSION_NON_INTRUSIVE(xerxes::SnoopConfig, line_num, assoc,
                                       max_burst_inv, ranges, eviction);

namespace xerxes {
// 1-to-1 device, used ahead a memory endpoint to perform snooping.
class Snoop : public Device {
    // Abstract class for eviction policy.
    class SnoopEviction {
      protected:
        size_t size;
        size_t assoc;
        size_t setn;

      public:
        std::function<bool(size_t, size_t)> is_valid;
        std::function<size_t(size_t, size_t)> sharer_count;

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

    // LFI (Least Frequently Inserted address) eviction policy.
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

    class RandomPolicy : public SnoopEviction {
        std::mt19937 rng;

      public:
        RandomPolicy() : SnoopEviction() {
            std::random_device rd;
            rng.seed(rd());
        }

        ssize_t find_victim(size_t set_i, bool do_evict) override {
            std::vector<size_t> valid;
            for (size_t i = 0; i < assoc; ++i) {
                if (is_valid(set_i, i))
                    valid.push_back(i);
            }
            if (valid.empty())
                return -1;
            auto victim = valid[rng() % valid.size()];
            return victim;
        }
    };

    class FewestSharers : public SnoopEviction {
      public:
        FewestSharers() : SnoopEviction() {}

        ssize_t find_victim(size_t set_i, bool do_evict) override {
            ssize_t victim = -1;
            size_t min_sharers = SIZE_MAX;
            for (size_t i = 0; i < assoc; ++i) {
                if (!is_valid(set_i, i))
                    continue;
                size_t cnt = sharer_count(set_i, i);
                if (cnt < min_sharers) {
                    min_sharers = cnt;
                    victim = i;
                }
            }
            return victim;
        }
    };

    class FewestSharersThenLIFO : public LIFO {
      public:
        FewestSharersThenLIFO() : LIFO() {}

        ssize_t find_victim(size_t set_i, bool do_evict) override {
            size_t min_sharers = SIZE_MAX;
            for (size_t i = 0; i < assoc; ++i) {
                if (!is_valid(set_i, i))
                    continue;
                size_t cnt = sharer_count(set_i, i);
                if (cnt < min_sharers)
                    min_sharers = cnt;
            }
            auto &q = queues[set_i];
            for (auto it = q.begin(); it != q.end(); ++it) {
                if (is_valid(set_i, *it) &&
                    sharer_count(set_i, *it) == min_sharers) {
                    auto victim = *it;
                    if (do_evict)
                        q.erase(it);
                    return victim;
                }
            }
            return -1;
        }
    };

    // Set-associative snoop cache.
    size_t line_num;
    size_t assoc;
    size_t set_num;

    size_t max_burst_inv;

    SnoopEviction *eviction;
    bool log_inv = false;
    TimedEventLog eviction_log_;

    enum State {
        EXCLUSIVE,   // single owner, clean
        SHARED,      // multiple hosts sharing
        MODIFIED,    // single owner, dirty
        WAIT_DRAM,
        EVICTING,
        INVALID,
    };

    struct Line {
        Addr addr;
        TopoID owner;
        std::set<TopoID> sharers;
        bool dirty;
        bool valid;
        State state;
    };

    struct WaitEntry {
        Packet pkt;
        size_t target_way; // < assoc → waiting for this way; == assoc → any
    };

    std::vector<std::vector<Line>> cache;
    std::vector<std::map<PktID, WaitEntry>> waiting;
    std::vector<std::pair<Addr, Addr>> ranges;

    // Statistics.
    std::unordered_map<TopoID, double> host_trig_conflict_count;
    std::unordered_map<size_t, double> burst_inv_size_count;
    std::unordered_map<Addr, size_t> evict_count;

    size_t back_inv_count = 0;
    size_t total_inv_packets = 0;
    std::vector<size_t> evict_sharers_list;

    size_t set_of(Addr addr) { return (addr / 64) % set_num; }

    ssize_t hit_addr(Addr addr) {
        auto set_i = set_of(addr);
        for (ssize_t i = 0; i < (ssize_t)assoc; ++i)
            if (cache[set_i][i].valid && cache[set_i][i].addr == addr)
                return i;
        return -1;
    }

    ssize_t hit_addr_owner(Addr addr, TopoID owner) {
        auto set_i = set_of(addr);
        for (ssize_t i = 0; i < (ssize_t)assoc; ++i) {
            auto &line = cache[set_i][i];
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

    void update(Addr addr, size_t set_i, size_t way_i, TopoID owner,
                State state, bool valid, bool update_evict = true) {
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

    // Check how long a burst eviction starting at addr can be performed.
    // TODO: hard-coded block size 64
    std::pair<Addr, size_t> peek_burst_evict(Addr addr, TopoID owner) {
        size_t burst = 1;
        auto begin_addr = addr;
        auto end_addr = addr;
        bool flag = true;
        while (flag && begin_addr <= addr &&
               addr - begin_addr < max_burst_inv * 64) {
            auto way = hit_addr_owner(begin_addr - 64, owner);
            if (way != -1) {
                begin_addr -= 64;
                burst += 1;
            } else {
                flag = false;
            }
        }
        flag = true;
        while (flag && end_addr >= addr &&
               end_addr - addr < max_burst_inv * 64) {
            auto way = hit_addr_owner(end_addr + 64, owner);
            if (way != -1) {
                end_addr += 64;
                burst += 1;
            } else {
                flag = false;
            }
        }
        return {begin_addr, burst};
    }

    void conduct_burst_evict(Addr start, size_t burst, TopoID owner,
                             Tick tick) {
        std::set<TopoID> inv_targets;
        inv_targets.insert(owner);
        for (size_t i = 0; i < burst; ++i) {
            auto way = hit_addr_owner(start + i * 64, owner);
            if (way != -1) {
                auto &line = cache[set_of(start + i * 64)][way];
                if (evict_count.find(line.addr) == evict_count.end()) {
                    evict_count[line.addr] = 0;
                }
                evict_count[line.addr] += 1;
                line.state = EVICTING;
                if (eviction != nullptr)
                    eviction->on_evict(line.addr,
                                       set_of(start + i * 64),
                                       way);
                for (auto &sharer : line.sharers)
                    inv_targets.insert(sharer);
                evict_sharers_list.push_back(line.sharers.size());
            }
        }
        for (auto &target : inv_targets) {
            auto inv = PktBuilder()
                           .type(PacketType::INV)
                           .addr(start)
                           .payload(0)
                           .burst(burst)
                           .sent(tick)
                           .arrive(0)
                           .src(self)
                           .dst(target)
                           .is_rsp(false)
                           .build();
            XerxesLogger::debug()
                << name() << ": evict packet " << inv.id << ", addr " << start
                << ", burst " << burst << ", target " << target << std::endl;
            send_pkt(inv);
        }
        back_inv_count += 1;
        total_inv_packets += inv_targets.size();
        eviction_log_.log(tick, {(double)burst, (double)owner});
    }

    void evict(size_t set_i, Tick tick) {
        ASSERT(eviction != nullptr, name() + ": eviction policy is null");
        auto &set = cache[set_i];
        auto victim = eviction->find_victim(set_i, true);
        XerxesLogger::debug() << name() << ": evict victim [" << set_i << ": "
                              << victim << "]" << std::endl;
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
        auto set_i = set_of(pkt.addr);

        // 1. Same host already has it -> hit, return immediately
        auto way_i = hit_addr_owner(pkt.addr, pkt.src);
        if (way_i != -1) {
            std::swap(pkt.src, pkt.dst);
            pkt.is_rsp = true;
            send_pkt(pkt);
            return;
        }

        // 2. Different host has it -> conflict, evict them
        way_i = hit_addr(pkt.addr);
        if (way_i != -1) {
            auto &line = cache[set_i][way_i];
            if (host_trig_conflict_count.find(pkt.src) ==
                host_trig_conflict_count.end()) {
                host_trig_conflict_count[pkt.src] = 0;
            }
            host_trig_conflict_count[pkt.src] += 1;
            XerxesLogger::debug()
                << name() << ": pkt " << pkt.id << " conflict [" << set_i << ":"
                << way_i << "]" << std::endl;
            waiting[set_i].insert(
                {pkt.id, {pkt, (size_t)way_i}});
            auto peek = peek_burst_evict(line.addr, line.owner);
            conduct_burst_evict(peek.first, peek.second, line.owner,
                                pkt.arrive);
            return;
        }

        // 3. No hit -> allocate
        auto new_way_i = new_way(pkt.addr);
        if (new_way_i == -1) {
            if (host_trig_conflict_count.find(pkt.src) ==
                host_trig_conflict_count.end()) {
                host_trig_conflict_count[pkt.src] = 0;
            }
            host_trig_conflict_count[pkt.src] += 1;
            XerxesLogger::debug()
                << name() << ": pkt " << pkt.id << " wait evict [" << set_i
                << "]" << std::endl;
            waiting[set_i].insert({pkt.id, {pkt, assoc}});
            evict(set_i, pkt.arrive);
            return;
        }
        XerxesLogger::debug()
            << name() << ": pkt " << pkt.id << " allocate [" << set_i << ":"
            << new_way_i << "]" << std::endl;
        update(pkt.addr, set_i, new_way_i, pkt.src, WAIT_DRAM, true, true);
        send_pkt(pkt);
    }

    void invalidate_response(Packet pkt) {
        if (log_inv)
            pkt.log_stat();
        auto tick = pkt.arrive;
        auto addr = pkt.addr;
        auto burst = pkt.burst;
        for (size_t i = 0; i < burst; ++i) {
            auto set_i = set_of(addr + i * 64);
            auto way_i = hit_addr_owner(addr + i * 64, pkt.src);
            if (way_i != -1) {
                // Invalidate the line.
                update(0, set_i, way_i, -1, INVALID, false);

                // Check waiting for this evicted way.
                for (auto it = waiting[set_i].begin();
                     it != waiting[set_i].end(); ++it) {
                    auto &entry = it->second;
                    if (entry.target_way == (size_t)way_i ||
                        entry.target_way == assoc) {
                        auto &waiter = entry.pkt;
                        XerxesLogger::debug()
                            << name() << ": insert waiter pkt " << waiter.id
                            << " to [" << set_i << ":" << way_i << "]"
                            << std::endl;
                        update(waiter.addr, set_i, way_i, waiter.src,
                               WAIT_DRAM, true, true);
                        if (tick > waiter.arrive) {
                            waiter.delta_stat(SNOOP_EVICT_DELAY,
                                              (double)(tick - waiter.arrive));
                            waiter.arrive = tick;
                        }
                        send_pkt(waiter);
                        waiting[set_i].erase(it);
                        break;
                    }
                }
            }
        }
    }

    bool in_range(Addr addr) {
        for (auto &range : ranges) {
            if (addr >= range.first && addr <= range.second)
                return true;
        }
        return false;
    }

    void filter(Packet pkt) {
        XerxesLogger::debug()
            << "filter " << pkt.is_coherent() << " " << pkt.is_rsp << " "
            << in_range(pkt.addr) << std::endl;
        for (auto &range : ranges) {
            XerxesLogger::debug()
                << "range " << range.first << " " << range.second << std::endl;
        }
        if (pkt.is_coherent() && !pkt.is_rsp && in_range(pkt.addr)) {
            // A coherent request belongs to the address range of this snoop.
            coherent_request(pkt);
        } else if (pkt.type == PacketType::INV && pkt.is_rsp &&
                   pkt.dst == self) {
            // An INV response to this snoop.
            invalidate_response(pkt);
        } else {
            // Non-temporal or response. Directly send the packet.
            if (pkt.is_rsp) {
                auto tick = pkt.arrive;
                auto set_i = set_of(pkt.addr);
                auto way_i = hit_addr_owner(pkt.addr, pkt.dst);
                if (way_i != -1) {
                    auto &line = cache[set_i][way_i];
                    auto new_state =
                        line.state == SHARED ? SHARED : EXCLUSIVE;
                    if (line.state == SHARED)
                        line.sharers.insert(pkt.dst);
                    else
                        line.sharers.clear();
                    XerxesLogger::debug()
                        << name() << ": DRAM rsp pkt " << pkt.id << " hit ["
                        << set_i << ":" << way_i << "]" << std::endl;
                    update(pkt.addr, set_i, way_i, pkt.dst, new_state, true,
                           false);
                    if (waiting[set_i].size() > 0) {
                        // Try an eviction.
                        XerxesLogger::debug()
                            << " try evict [" << set_i << "]" << std::endl;
                        evict(set_i, tick);
                    }
                }
            }
            XerxesLogger::debug()
                << name() << " send packet " << pkt.id << std::endl;
            log_transit_normal(pkt);
            send_pkt(pkt);
        }
    }

  public:
    Snoop(Simulation *sim, const SnoopConfig &config,
          std::string name = "Snoop")
        : Device(sim, name), line_num(config.line_num), assoc(config.assoc),
          set_num(config.line_num / config.assoc),
          max_burst_inv(config.max_burst_inv), log_inv(false),
          eviction_log_(name + "_eviction", {"burst_size", "target_host"}) {
        sim->register_event_log(&eviction_log_);
        ASSERT(line_num % assoc == 0, "snoop: size % assoc != 0");
        cache.resize(set_num);
        for (auto &c : cache)
            c.resize(assoc,
                     Line{0, 0, {}, false, false, INVALID});
        waiting.resize(set_num);
        ranges = config.ranges;
        if (config.eviction == "FIFO") {
            eviction = new FIFO{};
        } else if (config.eviction == "LIFO") {
            eviction = new LIFO{};
        } else if (config.eviction == "LRU") {
            eviction = new LRU{};
        } else if (config.eviction == "MRU") {
            eviction = new MRU{};
        } else if (config.eviction == "LFI") {
            eviction = new LFI{};
        } else if (config.eviction == "Random") {
            eviction = new RandomPolicy{};
        } else if (config.eviction == "FewestSharers") {
            eviction = new FewestSharers{};
        } else if (config.eviction == "FewestSharersThenLIFO") {
            eviction = new FewestSharersThenLIFO{};
        } else {
            PANIC("Unknown eviction policy: " + config.eviction);
        }
        eviction->is_valid = [this](size_t set_i,
                                     size_t way_i) -> bool {
            return cache[set_i][way_i].valid;
        };
        eviction->sharer_count = [this](size_t set_i,
                                         size_t way_i) -> size_t {
            return cache[set_i][way_i].sharers.size();
        };
        eviction->init(line_num, assoc);
    }

    ~Snoop() {}

    void transit() override {
        auto pkt = receive_pkt();
        if (!pkt.is_rsp)
            XerxesLogger::debug()
                << name() << " receive packet " << pkt.id << std::endl;
        // filter all packets
        filter(pkt);
    }

    void collect_summary() override {
        double total_conflict = 0;
        for (auto &pair : host_trig_conflict_count) {
            auto &host = pair.first;
            auto &count = pair.second;
            total_conflict += count;
            device_summary("host_" + std::to_string(host) + "_conflict_count",
                           count);
        }
        device_summary("total_conflict_count", total_conflict);

        double avg_burst_inv = 0;
        double total_burst_inv = 0;
        for (auto &pair : burst_inv_size_count) {
            auto &burst = pair.first;
            auto &count = pair.second;
            avg_burst_inv += burst * count;
            total_burst_inv += count;
        }
        if (total_burst_inv > 0)
            avg_burst_inv /= total_burst_inv;
        device_summary("avg_burst_inv_size", avg_burst_inv);

        device_summary("back_inv_count", (double)back_inv_count);
        device_summary("total_inv_packets", (double)total_inv_packets);

        if (!evict_sharers_list.empty()) {
            double avg_sharers = 0;
            for (auto &s : evict_sharers_list)
                avg_sharers += s;
            avg_sharers /= evict_sharers_list.size();
            device_summary("avg_evict_sharers", avg_sharers);

            std::map<size_t, size_t> sharer_dist;
            for (auto &s : evict_sharers_list)
                sharer_dist[s] += 1;
            for (auto &pair : sharer_dist)
                device_summary("evict_sharers_" + std::to_string(pair.first),
                               pair.second);
        }

        if (!eviction_log_.empty()) {
            auto &rows = eviction_log_.rows();
            Tick window = 5000;
            std::map<Tick, size_t> window_counts;
            for (auto &row : rows) {
                Tick w = (row.first / window) * window;
                window_counts[w] += 1;
            }
            std::vector<double> rates;
            for (auto &wc : window_counts)
                rates.push_back((double)wc.second);
            if (!rates.empty()) {
                std::sort(rates.begin(), rates.end());
                device_summary("inv_rate_p50",
                    rates[rates.size() * 50 / 100]);
                device_summary("inv_rate_p99",
                    rates[rates.size() * 99 / 100]);
                device_summary("inv_rate_max", rates.back());
                double avg_rate = 0;
                for (auto r : rates) avg_rate += r;
                avg_rate /= rates.size();
                device_summary("inv_rate_avg", avg_rate);
            }
        }
    }

    void log_stats(std::ostream &os) override {
        double avg_burst_inv = 0;
        double total_burst_inv = 0;
        for (auto &pair : burst_inv_size_count) {
            auto &burst = pair.first;
            auto &count = pair.second;
            avg_burst_inv += burst * count;
            total_burst_inv += count;
        }
        if (total_burst_inv > 0)
            avg_burst_inv /= total_burst_inv;

        os << name() << " stats:" << std::endl;
        for (auto &pair : host_trig_conflict_count) {
            auto &host = pair.first;
            auto &count = pair.second;
            os << " * host " << host << " conflict count: " << count
               << std::endl;
        }
        os << " * average burst invalidation size: " << avg_burst_inv
           << std::endl;
        os << " * back invalidation count: " << back_inv_count << std::endl;
        os << " * total invalidation packets: " << total_inv_packets
           << std::endl;

        if (!evict_sharers_list.empty()) {
            double avg_sharers = 0;
            for (auto &s : evict_sharers_list)
                avg_sharers += s;
            avg_sharers /= evict_sharers_list.size();
            os << " * average evicted sharers: " << avg_sharers << std::endl;
        }

        std::map<size_t, size_t> evict_count_pdf;
        for (auto &pair : evict_count) {
            auto &count = pair.second;
            if (evict_count_pdf.find(count) == evict_count_pdf.end()) {
                evict_count_pdf[count] = 0;
            }
            evict_count_pdf[count] += 1;
        }
        os << " * Evict count distribution: " << std::endl;
        for (auto &pair : evict_count_pdf) {
            os << pair.first << "," << pair.second << std::endl;
        }

        if (!evict_sharers_list.empty()) {
            std::map<size_t, size_t> sharer_dist;
            for (auto &s : evict_sharers_list)
                sharer_dist[s] += 1;
            os << " * Evicted sharer distribution: " << std::endl;
            for (auto &pair : sharer_dist) {
                os << pair.first << "," << pair.second << std::endl;
            }
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
};

} // namespace xerxes

#endif // XERXES_SNOOP_HH
