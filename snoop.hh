#pragma once
#ifndef XERXES_SNOOP_HH
#define XERXES_SNOOP_HH

#include "device.hh"
#include "event_log.hh"
#include "utils.hh"

#include <algorithm>
#include <iterator>
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
    // How a burst eviction picks the lines that share one BISnp command.
    // "centered" (default) grows the run symmetrically around the victim, up
    // to max_burst_inv lines on each side; the block length is therefore at
    // most 2 * max_burst_inv + 1, and every config written before this switch
    // keeps its exact behaviour.  "run" treats max_burst_inv as the total
    // block length: it extends the contiguous run that contains the victim
    // (forward first, then backward) until the block reaches max_burst_inv
    // lines, so achievable lengths are 1..N instead of 1, 3, 5, ...
    std::string burst_mode = "centered";
    // Victim selection for block invalidation: "policy" lets the configured
    // eviction policy choose (historical behaviour), "longest_run" scans the
    // set for the evictable line with the longest contiguous run.
    std::string victim_select = "policy";
    std::vector<std::pair<Addr, Addr>> ranges;
    std::string eviction = "LRU";
    // Mix the line number before taking it modulo the set count.  Off by
    // default: the historical set_of() is (addr / 64) % set_num, so existing
    // configs must keep bucketing exactly as before.
    bool index_hash = false;
};
} // namespace xerxes

// Hand-written instead of TOML11_DEFINE_CONVERSION_NON_INTRUSIVE.  That macro
// reads every listed key with toml::find(), so appending a field to the list
// makes any config file written before the field existed throw from
// from_toml(); parse_config() calls find_or<SnoopConfig>() and silently
// substitutes a default-constructed SnoopConfig, discarding the whole [Snoop]
// section.  The documented keys therefore stay mandatory, while the newer
// index_hash switch is optional so old configs parse unchanged.
namespace toml {
template <> struct from<xerxes::SnoopConfig> {
    template <typename TC>
    static xerxes::SnoopConfig from_toml(const basic_value<TC> &v) {
        xerxes::SnoopConfig obj;
        obj.line_num = find<decltype(obj.line_num)>(v, "line_num");
        obj.assoc = find<decltype(obj.assoc)>(v, "assoc");
        obj.max_burst_inv =
            find<decltype(obj.max_burst_inv)>(v, "max_burst_inv");
        // Optional: unlike the keys above, omitting burst_mode must keep the
        // historical centered search rather than discard the [Snoop] section.
        obj.burst_mode = find_or<std::string>(v, "burst_mode", "centered");
        obj.victim_select =
            find_or<std::string>(v, "victim_select", "policy");
        obj.ranges = find<decltype(obj.ranges)>(v, "ranges");
        obj.eviction = find<decltype(obj.eviction)>(v, "eviction");
        // Optional: config files predating this switch omit the key entirely.
        obj.index_hash = find_or<bool>(v, "index_hash", false);
        return obj;
    }
};
} // namespace toml

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
        // A way is a legal victim only if it holds a resident line that is
        // not already being evicted or waiting on a DRAM response.  The
        // predicate lives in the policy so every find_victim() applies the
        // same filter to its own queue/histogram.
        std::function<bool(size_t, size_t)> is_evictable;
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
            auto &q = queues[set_i];
            // Only a resident, idle line may be chosen.  Invalid ways have no
            // owner to invalidate, and EVICTING/WAIT_DRAM ways already have a
            // request in flight.  The way stays queued; it is removed when the
            // line is really invalidated (on_invalidate).
            for (auto it = q.rbegin(); it != q.rend(); ++it) {
                if (!is_evictable(set_i, *it))
                    continue;
                auto victim = *it;
                if (do_evict)
                    q.erase(std::next(it).base());
                return victim;
            }
            return -1;
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
            auto &q = queues[set_i];
            for (auto it = q.begin(); it != q.end(); ++it) {
                if (!is_evictable(set_i, *it))
                    continue;
                auto victim = *it;
                if (do_evict)
                    q.erase(it);
                return victim;
            }
            return -1;
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
            auto &q = queues[set_i];
            for (auto it = q.begin(); it != q.end(); ++it) {
                if (!is_evictable(set_i, *it))
                    continue;
                auto victim = *it;
                if (do_evict)
                    q.erase(it);
                return victim;
            }
            return -1;
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
            ssize_t victim = -1;
            for (auto &e : queues[set_i]) {
                if (!is_evictable(set_i, e.way_i))
                    continue;
                size_t k = 0;
                if (insert_cnt.find(e.addr) != insert_cnt.end())
                    k = insert_cnt[e.addr];
                if (k < ecnt) {
                    ecnt = k;
                    victim = e.way_i;
                }
            }
            if (victim == -1)
                return -1;
            if (do_evict) {
                auto &q = queues[set_i];
                for (auto it = q.begin(); it != q.end(); ++it) {
                    if ((ssize_t)it->way_i == victim) {
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
                if (is_evictable(set_i, i))
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
                if (!is_evictable(set_i, i))
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
            // Compute the minimum over the candidate queue, not over every
            // valid way: a way that is not queued (e.g. an in-flight
            // WAIT_DRAM line) must not lower the bar to a value no queued
            // way can match, which would leave the caller with no victim.
            auto &q = queues[set_i];
            size_t min_sharers = SIZE_MAX;
            for (auto &w : q) {
                if (!is_evictable(set_i, w))
                    continue;
                size_t cnt = sharer_count(set_i, w);
                if (cnt < min_sharers)
                    min_sharers = cnt;
            }
            if (min_sharers == SIZE_MAX)
                return -1;
            for (auto it = q.begin(); it != q.end(); ++it) {
                if (is_evictable(set_i, *it) &&
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

    class FewestSharersThenFIFO : public FIFO {
      public:
        FewestSharersThenFIFO() : FIFO() {}

        ssize_t find_victim(size_t set_i, bool do_evict) override {
            auto &q = queues[set_i];
            size_t min_sharers = SIZE_MAX;
            for (auto &w : q) {
                if (!is_evictable(set_i, w))
                    continue;
                size_t cnt = sharer_count(set_i, w);
                if (cnt < min_sharers)
                    min_sharers = cnt;
            }
            if (min_sharers == SIZE_MAX)
                return -1;
            for (auto it = q.rbegin(); it != q.rend(); ++it) {
                if (is_evictable(set_i, *it) &&
                    sharer_count(set_i, *it) == min_sharers) {
                    auto victim = *it;
                    if (do_evict)
                        q.erase(std::next(it).base());
                    return victim;
                }
            }
            return -1;
        }
    };

    class FewestSharersThenLRU : public LRU {
      public:
        FewestSharersThenLRU() : LRU() {}

        ssize_t find_victim(size_t set_i, bool do_evict) override {
            auto &q = queues[set_i];
            size_t min_sharers = SIZE_MAX;
            for (auto &w : q) {
                if (!is_evictable(set_i, w))
                    continue;
                size_t cnt = sharer_count(set_i, w);
                if (cnt < min_sharers)
                    min_sharers = cnt;
            }
            if (min_sharers == SIZE_MAX)
                return -1;
            for (auto it = q.rbegin(); it != q.rend(); ++it) {
                if (is_evictable(set_i, *it) &&
                    sharer_count(set_i, *it) == min_sharers) {
                    auto victim = *it;
                    if (do_evict)
                        q.erase(std::next(it).base());
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

    // "centered" or "run"; see SnoopConfig::burst_mode.
    std::string burst_mode;

    // "policy" or "longest_run"; see SnoopConfig::victim_select.
    std::string victim_select;

    // Optional line-number mixing in set_of(); see SnoopConfig::index_hash.
    bool index_hash;

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

    // How often evict() ran; the denominator for policy-level victim counts.
    size_t evict_total_count_ = 0;

    // Clean eviction notifications handled by this snoop.
    size_t cln_evct_rx = 0;
    size_t cln_evct_free = 0;
    size_t cln_evct_drop_sharer = 0;
    size_t cln_evct_promote = 0;
    size_t cln_evct_stale = 0;
    size_t cln_evct_busy = 0;
    size_t cln_evct_dirty_reject = 0;
    size_t cln_evct_out_of_range = 0;

    // The set index is a pure function of the line address, and every cache
    // access, clean-evict handler and burst eviction goes through here.  By
    // default the low line bits pick the bucket, which couples the set index
    // to the address layout: changing set_num re-buckets every line and can
    // produce eviction spikes that are an artifact, not a capacity effect.
    // index_hash mixes the line number first so a set-count sweep samples
    // the same address stream evenly.
    size_t set_of(Addr addr) {
        size_t line = addr / 64;
        if (index_hash) {
            line ^= line >> 33;
            line *= 0xff51afd7ed558ccdULL;
            line ^= line >> 33;
        }
        return line % set_num;
    }

    ssize_t hit_addr(Addr addr) {
        auto set_i = set_of(addr);
        for (ssize_t i = 0; i < (ssize_t)assoc; ++i)
            if (cache[set_i][i].valid && cache[set_i][i].addr == addr)
                return i;
        return -1;
    }

    ssize_t find_addr_owner(Addr addr, TopoID owner, bool count_as_hit) {
        auto set_i = set_of(addr);
        for (ssize_t i = 0; i < (ssize_t)assoc; ++i) {
            auto &line = cache[set_i][i];
            if (line.valid && line.addr == addr && line.owner == owner) {
                if (count_as_hit && eviction)
                    eviction->on_hit(addr, set_i, i);
                return i;
            }
        }
        return -1;
    }

    ssize_t hit_addr_owner(Addr addr, TopoID owner) {
        return find_addr_owner(addr, owner, true);
    }

    ssize_t probe_addr_owner(Addr addr, TopoID owner) {
        return find_addr_owner(addr, owner, false);
    }

    void note_conflict(TopoID host) {
        if (host_trig_conflict_count.find(host) == host_trig_conflict_count.end())
            host_trig_conflict_count[host] = 0;
        host_trig_conflict_count[host] += 1;
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
        line.dirty = state == MODIFIED;
        if (!valid || state != SHARED)
            line.sharers.clear();
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
    // Block length = total lines, not lines per side: extend the contiguous
    // run that contains the victim, forward first and then backward.
    std::pair<Addr, size_t> peek_burst_run(Addr addr, TopoID owner) {
        size_t max_len = max_burst_inv == 0 ? 1 : max_burst_inv;
        size_t burst = 1;
        auto begin_addr = addr;
        auto end_addr = addr;
        while (burst < max_len) {
            if (probe_addr_owner(end_addr + 64, owner) == -1)
                break;
            end_addr += 64;
            burst += 1;
        }
        while (burst < max_len) {
            if (begin_addr < 64)
                break;
            if (probe_addr_owner(begin_addr - 64, owner) == -1)
                break;
            begin_addr -= 64;
            burst += 1;
        }
        return {begin_addr, burst};
    }

    std::pair<Addr, size_t> peek_burst_evict(Addr addr, TopoID owner) {
        if (burst_mode == "run")
            return peek_burst_run(addr, owner);
        size_t burst = 1;
        auto begin_addr = addr;
        auto end_addr = addr;
        bool flag = true;
        while (flag && begin_addr <= addr &&
               addr - begin_addr < max_burst_inv * 64) {
            auto way = probe_addr_owner(begin_addr - 64, owner);
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
            auto way = probe_addr_owner(end_addr + 64, owner);
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
                             Tick tick, size_t victim_sharers,
                             size_t set_i, Addr victim_addr) {
        std::set<TopoID> inv_targets;
        if (owner >= 0)
            inv_targets.insert(owner);
        for (size_t i = 0; i < burst; ++i) {
            auto way = probe_addr_owner(start + i * 64, owner);
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
                if (line.owner >= 0)
                    inv_targets.insert(line.owner);
                for (auto &sharer : line.sharers)
                    if (sharer >= 0)
                        inv_targets.insert(sharer);
                evict_sharers_list.push_back(line.sharers.size());
            }
        }
        size_t sent_inv = 0;
        for (auto &target : inv_targets) {
            if (target < 0 || target == self)
                continue;
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
            ++sent_inv;
        }
        back_inv_count += 1;
        total_inv_packets += sent_inv;
        eviction_log_.log(
            tick,
            {(double)burst, (double)owner, (double)victim_sharers,
             (double)set_i, (double)victim_addr});
    }

    void evict(size_t set_i, Tick tick) {
        ASSERT(eviction != nullptr, name() + ": eviction policy is null");
        ++evict_total_count_;
        auto &set = cache[set_i];
        // Select without popping: the victim keeps its queue slot until the
        // line is really invalidated in free_way().  Popping here used to lose
        // the way whenever its DRAM response settled the line in place, which
        // left a full set with no policy candidate.
        auto victim = eviction->find_victim(set_i, false);
        if (victim_select == "longest_run") {
            // Block-length-prioritised selection: prefer the evictable line
            // whose contiguous run is the longest, so a single InvBlk clears
            // as many cachelines as possible.  Ties keep the policy victim.
            ssize_t best = -1;
            size_t best_len = 0;
            if (victim != -1 && eviction->is_evictable(set_i, victim)) {
                best = victim;
                best_len = peek_burst_evict(set[victim].addr,
                                            set[victim].owner)
                               .second;
            }
            for (size_t w = 0; w < assoc; ++w) {
                if ((ssize_t)w == best)
                    continue;
                auto &cand = set[w];
                if (!cand.valid)
                    continue;
                if (!eviction->is_evictable(set_i, w))
                    continue;
                size_t len = peek_burst_evict(cand.addr, cand.owner).second;
                if (len > best_len) {
                    best = (ssize_t)w;
                    best_len = len;
                }
            }
            if (best != -1)
                victim = best;
        }
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

            conduct_burst_evict(peek.first, peek.second, line.owner, tick,
                                line.sharers.size(), set_i, line.addr);
        } else {
            // No policy candidate.  A free way can still exist (e.g. one was
            // released after the allocation parked), so hand it over if so.
            reclaim_free_way(set_i, tick);
        }
    }

    void coherent_request(Packet pkt) {
        auto set_i = set_of(pkt.addr);

        // The snoop is a coherence filter, not a data cache: every coherent
        // request must be served by the memory device.  A hit only supplies
        // the metadata (the holders already recorded for the line) that the
        // response will need.
        auto way_i = hit_addr(pkt.addr);
        if (way_i != -1) {
            auto &line = cache[set_i][way_i];
            if (line.state == EVICTING) {
                // An eviction of this line is already in flight.  Leave its
                // bookkeeping alone: free_way() will hand the way to the
                // waiter parked by that eviction.  This request still goes to
                // memory, the only place that can serve it.
                XerxesLogger::debug()
                    << name() << ": pkt " << pkt.id << " forward evicting ["
                    << set_i << ":" << way_i << "]" << std::endl;
                send_pkt(pkt);
                return;
            }
            // A write needs the line to itself, so any other holder is evicted
            // first through the existing conflict path; free_way() then wakes
            // the parked request and forwards it to memory.
            bool other_holders =
                line.owner != pkt.src || !line.sharers.empty();
            if (pkt.is_write() && other_holders) {
                note_conflict(pkt.src);
                XerxesLogger::debug()
                    << name() << ": pkt " << pkt.id << " conflict [" << set_i
                    << ":" << way_i << "]" << std::endl;
                waiting[set_i].insert({pkt.id, {pkt, (size_t)way_i}});
                auto peek = peek_burst_evict(line.addr, line.owner);
                conduct_burst_evict(peek.first, peek.second, line.owner,
                                    pkt.arrive, line.sharers.size(), set_i,
                                    line.addr);
                return;
            }
            // Forward to memory while remembering the previous holders as
            // sharers.  The response branch turns a non-empty sharer set into
            // SHARED and an empty one into EXCLUSIVE.
            std::set<TopoID> holders = line.sharers;
            holders.erase(pkt.src);
            if (line.owner >= 0 && line.owner != pkt.src)
                holders.insert(line.owner);
            XerxesLogger::debug()
                << name() << ": pkt " << pkt.id << " forward hit [" << set_i
                << ":" << way_i << "]" << std::endl;
            update(pkt.addr, set_i, way_i, pkt.src, WAIT_DRAM, true, true);
            cache[set_i][way_i].sharers = holders;
            send_pkt(pkt);
            return;
        }

        // 3. No hit -> allocate
        auto new_way_i = new_way(pkt.addr);
        if (new_way_i == -1) {
            note_conflict(pkt.src);
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

    // Free the line at [set_i:way_i] and wake up one waiter for that way.
    void free_way(size_t set_i, size_t way_i, Tick tick) {
        // Invalidate the line.
        update(0, set_i, way_i, -1, INVALID, false);

        // Check waiting for this evicted way.
        for (auto it = waiting[set_i].begin(); it != waiting[set_i].end();
             ++it) {
            auto &entry = it->second;
            if (entry.target_way == way_i || entry.target_way == assoc) {
                auto &waiter = entry.pkt;
                XerxesLogger::debug()
                    << name() << ": insert waiter pkt " << waiter.id
                    << " to [" << set_i << ":" << way_i << "]" << std::endl;
                update(waiter.addr, set_i, way_i, waiter.src, WAIT_DRAM, true,
                       true);
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

    // If a way is already free and an allocation waiter is parked in this
    // set, hand the way over immediately.  The wake does not depend on an INV
    // response: a response that no longer matches its line (the line may have
    // been freed in the meantime) must not strand the waiter.
    void reclaim_free_way(size_t set_i, Tick tick) {
        bool assoc_waiter = false;
        for (auto &kv : waiting[set_i]) {
            if (kv.second.target_way == assoc) {
                assoc_waiter = true;
                break;
            }
        }
        if (!assoc_waiter)
            return;
        for (size_t w = 0; w < assoc; ++w) {
            if (!cache[set_i][w].valid) {
                free_way(set_i, w, tick);
                return;
            }
        }
    }

    void invalidate_response(Packet pkt) {
        if (log_inv)
            pkt.log_stat();
        auto tick = pkt.arrive;
        auto addr = pkt.addr;
        auto burst = pkt.burst;
        for (size_t i = 0; i < burst; ++i) {
            auto set_i = set_of(addr + i * 64);
            auto way_i = probe_addr_owner(addr + i * 64, pkt.src);
            if (way_i != -1)
                free_way(set_i, (size_t)way_i, tick);
            reclaim_free_way(set_i, tick);
        }
    }

    bool in_range(Addr addr) {
        for (auto &range : ranges) {
            if (addr >= range.first && addr <= range.second)
                return true;
        }
        return false;
    }

    // Handle a clean eviction notification.  It is a one-way notification
    // from a host cache that silently dropped a clean line; the snoop sits
    // inline on the path to memory, so it consumes the packet here.
    void handle_clean_evict(Packet pkt) {
        auto addr = pkt.addr;
        ++cln_evct_rx;
        if (!in_range(addr)) {
            ++cln_evct_out_of_range;
            return;
        }
        auto set_i = set_of(addr);
        auto way = hit_addr(addr);
        if (way == -1) {
            // The snoop already evicted this line; the notification is stale.
            ++cln_evct_stale;
            return;
        }
        auto way_i = (size_t)way;
        auto &line = cache[set_i][way_i];
        if (line.state == WAIT_DRAM || line.state == EVICTING) {
            // The way belongs to an in-flight waiter; it must not be freed.
            ++cln_evct_busy;
            return;
        }
        if (line.owner == pkt.src) {
            if (line.dirty || line.state == MODIFIED) {
                // A clean eviction must never drop a dirty line.
                ++cln_evct_dirty_reject;
                return;
            }
            if (!line.sharers.empty()) {
                // Promote a sharer to owner instead of freeing the line.
                auto it = line.sharers.begin();
                auto new_owner = *it;
                line.sharers.erase(it);
                auto new_state = line.sharers.empty() ? EXCLUSIVE : SHARED;
                update(addr, set_i, way_i, new_owner, new_state, true, true);
                ++cln_evct_promote;
            } else {
                free_way(set_i, way_i, pkt.arrive);
                ++cln_evct_free;
            }
        } else if (line.sharers.find(pkt.src) != line.sharers.end()) {
            line.sharers.erase(pkt.src);
            ++cln_evct_drop_sharer;
        } else {
            ++cln_evct_stale;
        }
    }

    void filter(Packet pkt) {
        if (pkt.type == PacketType::CLN_EVCT) {
            // Clean eviction notifications are consumed by the snoop.  They
            // never reach memory, so neither in_range nor pkt.dst (the home
            // node id) applies here.
            handle_clean_evict(pkt);
            return;
        }
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
                    // The memory is write-through: a DRAM response always
                    // carries a clean copy, so the installed line is clean and
                    // owned, and only a write response conveys write grants.
                    // A line that still has sharers is shared; only a line
                    // without them is exclusive.
                    auto new_state =
                        cache[set_i][way_i].sharers.empty() ? EXCLUSIVE
                                                            : SHARED;
                    pkt.exclusive_grant = pkt.is_write();
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
          max_burst_inv(config.max_burst_inv),
          burst_mode(config.burst_mode),
          victim_select(config.victim_select),
          index_hash(config.index_hash),
          log_inv(false),
          eviction_log_(name + "_eviction",
                        {"burst_size", "target_host", "victim_sharers",
                         "set_idx", "victim_addr"}) {
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
        } else if (config.eviction == "FewestSharersThenFIFO" ||
                   config.eviction == "FS+FIFO") {
            eviction = new FewestSharersThenFIFO{};
        } else if (config.eviction == "FewestSharersThenLRU" ||
                   config.eviction == "FS+LRU") {
            eviction = new FewestSharersThenLRU{};
        } else if (config.eviction == "FS+LIFO") {
            eviction = new FewestSharersThenLIFO{};
        } else {
            PANIC("Unknown eviction policy: " + config.eviction);
        }
        eviction->is_evictable = [this](size_t set_i,
                                         size_t way_i) -> bool {
            auto &line = cache[set_i][way_i];
            return line.valid && line.state != EVICTING &&
                   line.state != WAIT_DRAM;
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
        device_summary("evict_total_count", (double)evict_total_count_);

        device_summary("cln_evct_rx", (double)cln_evct_rx);
        device_summary("cln_evct_free", (double)cln_evct_free);
        device_summary("cln_evct_drop_sharer", (double)cln_evct_drop_sharer);
        device_summary("cln_evct_promote", (double)cln_evct_promote);
        device_summary("cln_evct_stale", (double)cln_evct_stale);
        device_summary("cln_evct_busy", (double)cln_evct_busy);
        device_summary("cln_evct_dirty_reject",
                       (double)cln_evct_dirty_reject);
        device_summary("cln_evct_out_of_range",
                       (double)cln_evct_out_of_range);

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

        std::map<size_t, size_t> resident_sharer_dist;
        double avg_resident_sharers = 0;
        size_t resident_cnt = 0;
        for (auto &set : cache) {
            for (auto &line : set) {
                if (!line.valid)
                    continue;
                size_t sharers = line.sharers.size();
                resident_sharer_dist[sharers] += 1;
                avg_resident_sharers += sharers;
                resident_cnt += 1;
            }
        }
        if (resident_cnt > 0) {
            avg_resident_sharers /= resident_cnt;
            device_summary("avg_resident_sharers", avg_resident_sharers);
            for (auto &pair : resident_sharer_dist) {
                device_summary(
                    "resident_sharers_" + std::to_string(pair.first),
                    pair.second);
            }
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

        std::map<size_t, size_t> resident_sharer_dist;
        for (auto &set : cache) {
            for (auto &line : set) {
                if (!line.valid)
                    continue;
                resident_sharer_dist[line.sharers.size()] += 1;
            }
        }
        if (!resident_sharer_dist.empty()) {
            os << " * Resident sharer distribution: " << std::endl;
            for (auto &pair : resident_sharer_dist) {
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
