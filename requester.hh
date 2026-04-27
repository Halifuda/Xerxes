#pragma once
#ifndef XERXES_REQUESTER_HH
#define XERXES_REQUESTER_HH

#include "address_system.hh"
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
class RequesterConfig {
  public:
    size_t q_capacity = 32;
    size_t cache_capacity = 8192;
    Tick cache_delay = 12;
    Tick issue_delay = 0;
    bool coherent = false;
    size_t burst_size = 1;
    size_t block_size = 64;
    std::string interleave_type = "stream";
    size_t interleave_param = 5;
    double hot_req_ratio = 0.5;
    double hot_region_ratio = 0.5;
    std::string trace_file = "";
    uint64_t random_seed = 0;
    Addr hpa_base = 0;
    size_t hpa_size = 1 << 30;
    double wr_ratio = 0.5;
};
} // namespace xerxes

TOML11_DEFINE_CONVERSION_NON_INTRUSIVE(xerxes::RequesterConfig, q_capacity,
                                       cache_capacity, cache_delay, issue_delay,
                                       coherent, burst_size, block_size,
                                       interleave_type, interleave_param,
                                       hot_req_ratio, hot_region_ratio,
                                       trace_file, random_seed, hpa_base,
                                       hpa_size, wr_ratio);

namespace xerxes {
class Requester : public Device {
    class HpaGenerator {
      protected:
        std::random_device rd;
        std::ranlux48 gen;
        std::uniform_real_distribution<> uni;
        uint64_t actual_seed;

      public:
        struct Request {
            Addr hpa;
            bool is_write;
            Tick tick;
        };

        Addr hpa_base = 0;
        size_t hpa_size = 0;
        double wr_ratio = 0.5;
        size_t block_size;

      public:
        HpaGenerator(size_t block_size = 64, uint64_t seed = 0)
            : block_size(block_size) {
            if (seed != 0) {
                gen.seed(seed);
                actual_seed = seed;
            } else {
                actual_seed = rd();
                gen.seed(actual_seed);
            }
            uni = std::uniform_real_distribution<>(0, 1);
        }
        uint64_t get_seed() const { return actual_seed; }

        virtual Request next() = 0;
        virtual bool eof() = 0;
    };

    class Trace : public HpaGenerator {
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
            size_t block_size = 64, uint64_t seed = 0)
            : HpaGenerator(block_size, seed), decoder(decoder) {
            this->trace_file.open(trace_file);
            ASSERT(this->trace_file.is_open(),
                   std::string{"Cannot open trace file"} + trace_file);
        }
        bool eof() { return trace_file.eof(); }
        Request next() {
            auto req = decoder(trace_file);
            return {req.addr, req.is_write, req.tick};
        }
    };

    class Stream : public HpaGenerator {
        size_t total_count;
        size_t cur_count = 0;

      public:
        Stream(size_t total_count, size_t block_size = 64, uint64_t seed = 0)
            : HpaGenerator(block_size, seed), total_count(total_count) {}
        bool eof() { return cur_count == total_count; }
        Request next() {
            auto n_blocks = hpa_size / block_size;
            auto addr =
                hpa_base + (cur_count % n_blocks) * block_size;
            bool is_write = uni(gen) < wr_ratio;
            cur_count++;
            return {addr, is_write, 0};
        }
    };

    class Random : public HpaGenerator {
        size_t total_count;
        size_t cur_count = 0;
        std::normal_distribution<> norm;
        double hot_req_ratio;
        double hot_region_ratio;

        Addr hot_start = 0;
        size_t hot_capacity = 0;

        void configure_hot_region() {
            auto blocks = hpa_size / block_size;
            if (blocks == 0) {
                hot_start = hpa_base;
                hot_capacity = 0;
                return;
            }

            auto hot_blocks = static_cast<size_t>(
                std::max<size_t>(1, std::round(blocks * hot_region_ratio)));
            hot_blocks = std::min(hot_blocks, blocks);
            auto max_start = blocks - hot_blocks;
            size_t start_block = 0;
            if (max_start > 0) {
                std::uniform_int_distribution<size_t> dist(0, max_start);
                start_block = dist(gen);
            }
            hot_start = hpa_base + start_block * block_size;
            hot_capacity = hot_blocks * block_size;
        }

      public:
        Random(size_t total_count, size_t block_size = 64,
               double hot_req_ratio = 0.5, double hot_region_ratio = 0.5,
               uint64_t seed = 0)
            : HpaGenerator(block_size, seed), total_count(total_count),
              hot_req_ratio(hot_req_ratio), hot_region_ratio(hot_region_ratio) {
            norm = std::normal_distribution<>(0.5, 0.5);
        }

        void finalize() { configure_hot_region(); }
        bool eof() { return cur_count == total_count; }
        Request next() {
            Addr addr = hpa_base;

            auto total_blocks = hpa_size / block_size;
            auto hot_blocks = hot_capacity / block_size;
            auto pre_hot_blocks = (hot_start - hpa_base) / block_size;
            auto post_hot_blocks =
                total_blocks - pre_hot_blocks - hot_blocks;

            bool use_hot = uni(gen) < hot_req_ratio;

            if (use_hot || hot_blocks == total_blocks || hot_blocks == 0) {
                size_t hot_offset =
                    (hot_blocks > 0)
                        ? static_cast<size_t>(uni(gen) * hot_blocks)
                        : 0;
                auto chosen_block = pre_hot_blocks + hot_offset;
                addr = hpa_base + chosen_block * block_size;
            } else {
                auto cold_blocks = pre_hot_blocks + post_hot_blocks;
                if (cold_blocks == 0) {
                    addr = hot_start;
                } else {
                    auto pick = static_cast<size_t>(uni(gen) * cold_blocks);
                    if (pick < pre_hot_blocks) {
                        addr = hpa_base + pick * block_size;
                    } else {
                        auto post_idx = pick - pre_hot_blocks;
                        addr = hot_start + hot_capacity +
                               post_idx * block_size;
                    }
                }
            }

            bool is_write = uni(gen) < wr_ratio;
            cur_count++;
            return {addr, is_write, 0};
        }
    };

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

    HpaGenerator *hpa_gen_;
    IssueQueue q;
    FakeLRUCache cache;
    Tick cur = 0;
    Tick last_arrive = 0;
    size_t cur_cnt = 0;
    Tick issue_delay;
    bool coherent;
    size_t burst_size = 1;
    size_t block_size = 64;

    std::unordered_map<std::string, std::unordered_map<std::string, double>>
        stats;

  public:
    Requester(Simulation *sim, const RequesterConfig &config,
              std::string name = "Host")
        : Device(sim, name), q(config.q_capacity),
          cache(config.cache_capacity, config.cache_delay),
          issue_delay(config.issue_delay), coherent(config.coherent),
          burst_size(config.burst_size), block_size(config.block_size) {
        XerxesLogger::debug()
            << "Interleave param " << config.interleave_param << std::endl;
        if (config.interleave_type == "stream") {
            hpa_gen_ = new Stream{config.interleave_param, config.block_size,
                                     config.random_seed};
        } else if (config.interleave_type == "random") {
            hpa_gen_ =
                new Random{config.interleave_param, block_size, 0.5, 0.5,
                           config.random_seed};
        } else if (config.interleave_type == "hotcold") {
            hpa_gen_ =
                new Random{config.interleave_param, block_size,
                           config.hot_req_ratio, config.hot_region_ratio,
                           config.random_seed};
        } else if (config.interleave_type == "trace") {
            hpa_gen_ = new Trace{config.trace_file, {}, block_size,
                                   config.random_seed};
        } else {
            PANIC("Unknown interleave type: " + config.interleave_type);
        }
        hpa_gen_->hpa_base = config.hpa_base;
        hpa_gen_->hpa_size = config.hpa_size;
        hpa_gen_->wr_ratio = config.wr_ratio;
        if (auto rnd = dynamic_cast<Random *>(hpa_gen_))
            rnd->finalize();
        stats["-1"]["Cache evict count"] = 0;
        stats["-1"]["Cache hit count"] = 0;
    }

    Requester &set_hpa_range(Addr base, size_t size) {
        hpa_gen_->hpa_base = base;
        hpa_gen_->hpa_size = size;
        if (auto rnd = dynamic_cast<Random *>(hpa_gen_))
            rnd->finalize();
        return *this;
    }

    void transit() override {
        auto pkt = receive_pkt();
        if (pkt.dst == self) {
            if (pkt.is_rsp) {
                XerxesLogger::debug()
                    << name() << " receive packet " << pkt.id
                    << ", issue queue is full? " << q.full() << std::endl;
                last_arrive = pkt.arrive;
                if (coherent)
                    cache.insert(pkt.addr);

                auto dram_key = std::to_string(pkt.src);
                if (stats.find(dram_key) == stats.end()) {
                    stats[dram_key] = {};
                    stats[dram_key]["Count"] = 0;
                    stats[dram_key]["Bandwidth"] = 0;
                    stats[dram_key]["Average latency"] = 0;
                    stats[dram_key]["Average wait for evict"] = 0;
                }
                stats[dram_key]["Count"] += 1;
                stats[dram_key]["Bandwidth"] += pkt.burst * 64;
                stats[dram_key]["Average latency"] += pkt.arrive - pkt.sent;
                stats[dram_key]["Average wait for evict"] +=
                    pkt.get_stat(SNOOP_EVICT_DELAY);

                if (q.full())
                    register_issue_event(pkt.arrive);
                q.pop(pkt);
                pkt.log_stat();
            } else if (pkt.type == INV) {
                if (coherent) {
                    cache.invalidate(pkt.addr);
                    stats["-1"]["Cache evict count"] += 1;
                    std::swap(pkt.src, pkt.dst);
                    pkt.is_rsp = true;
                    pkt.payload = block_size * pkt.burst;
                    pkt.arrive += cache.delay;
                    pkt.delta_stat(NormalStatType::HOST_INV_DELAY, cache.delay);
                    cur = std::max(cur, pkt.arrive) + issue_delay;
                    send_pkt(pkt);
                }
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
            sum = stats["-1"][name];
        } else {
            for (auto &pair : stats) {
                sum += pair.second[name];
            }
        }
        return sum;
    }

    void collect_summary() override {
        double agg_bw = 0;
        double agg_cnt = 0;
        double agg_lat = 0;
        double agg_wait = 0;
        if (last_arrive > 0) {
            for (auto &pair : stats) {
                if (pair.first == "-1")
                    continue;
                agg_cnt += pair.second["Count"];
                auto ep_bw =
                    pair.second["Bandwidth"] / (double)(last_arrive);
                agg_bw += ep_bw;
                agg_lat += pair.second["Average latency"];
                agg_wait += pair.second["Average wait for evict"];
                auto ep = pair.first;
                device_summary(ep + ":bw_gbps", ep_bw);
                device_summary(ep + ":avg_latency_ns",
                               pair.second["Average latency"] /
                                   pair.second["Count"]);
                device_summary(ep + ":avg_evict_wait_ns",
                               pair.second["Average wait for evict"] /
                                   pair.second["Count"]);
            }
        }
        device_summary("random_seed", (double)hpa_gen_->get_seed());
        device_summary("req_count", (double)cur_cnt);
        device_summary("cache_hit_count", stats["-1"]["Cache hit count"]);
        device_summary("cache_evict_count", stats["-1"]["Cache evict count"]);
        if (agg_cnt > 0) {
            device_summary("bw_gbps", agg_bw);
            device_summary("avg_latency_ns", agg_lat / agg_cnt);
            device_summary("avg_evict_wait_ns", agg_wait / agg_cnt);
        }
    }

    void log_stats(std::ostream &os) override {
        double agg_bw = 0;
        double agg_cnt = 0;
        double agg_lat = 0;
        double agg_wait = 0;
        if (last_arrive > 0) {
            for (auto &pair : stats) {
                if (pair.first == "-1")
                    continue;
                agg_cnt += pair.second["Count"];
                auto ep_bw =
                    pair.second["Bandwidth"] / (double)(last_arrive);
                agg_bw += ep_bw;
                agg_lat += pair.second["Average latency"];
                agg_wait += pair.second["Average wait for evict"];
            }
        }
        os << name() << " stats: " << std::endl;
        os << " * Payload size: " << block_size << " bytes" << std::endl;
        os << " * Issued packets: " << cur_cnt << std::endl;
        os << " * Evict count: " << stats["-1"]["Cache evict count"]
           << std::endl;
        os << " * Hit count: " << stats["-1"]["Cache hit count"] << std::endl;
        for (auto &pair : stats) {
            if (pair.first == "-1")
                continue;
            os << " * Endpoint " << pair.first << ": " << std::endl;
            os << "   - Bandwidth (GB/s): "
               << pair.second["Bandwidth"] / (double)(last_arrive) << std::endl;
            os << "   - Average latency (ns): "
               << pair.second["Average latency"] / pair.second["Count"]
               << std::endl;
            os << "   - Average wait for evict (ns): "
               << pair.second["Average wait for evict"] / pair.second["Count"]
               << std::endl;
        }
        os << " * Aggregate: " << std::endl;
        os << "   - Bandwidth (GB/s): " << agg_bw << std::endl;
        os << "   - Average latency (ns): " << agg_lat / agg_cnt << std::endl;
        os << "   - Average wait for evict (ns): " << agg_wait / agg_cnt
           << std::endl;
    }

    bool step(bool coherent) {
        static bool ended = false;
        if (!hpa_gen_->eof()) {
            if (q.full()) {
                if (cur < last_arrive)
                    cur = last_arrive;
                return false;
            }
            auto req = hpa_gen_->next();
            auto qr = sim->address_system()->query(req.hpa);
            if (!qr.valid) {
                XerxesLogger::warning()
                    << name() << ": no address mapping for HPA 0x" << std::hex
                    << req.hpa << std::dec << std::endl;
                return true;
            }
            cur += issue_delay;
            if (req.tick != 0)
                cur = req.tick;
            if (coherent && cache.hit(req.hpa)) {
                auto dram_key = std::to_string(qr.dpid);
                if (stats.find(dram_key) == stats.end()) {
                    stats[dram_key] = {};
                    stats[dram_key]["Count"] = 0;
                    stats[dram_key]["Bandwidth"] = 0;
                    stats[dram_key]["Average latency"] = 0;
                    stats[dram_key]["Average wait for evict"] = 0;
                }
                stats[dram_key]["Count"] += 1;
                stats[dram_key]["Bandwidth"] += burst_size * 64;
                stats[dram_key]["Average latency"] += cache.delay;
                stats["-1"]["Cache hit count"] += 1;

                XerxesLogger::debug()
                    << name() << " cache hit: " << req.hpa << "," << cur << ","
                    << cur + cache.delay << std::endl;
                cur += cache.delay;
                last_arrive = cur;
                return true;
            }
            if (coherent)
                cur += cache.delay;
            auto type = req.is_write
                            ? (coherent ? PacketType::WT : PacketType::NT_WT)
                            : (coherent ? PacketType::RD : PacketType::NT_RD);
            auto pkt =
                PktBuilder()
                    .src(self)
                    .dst(qr.dpid)
                    .addr(req.hpa)
                    .dpa(qr.dpa)
                    .sent(cur)
                    .payload(type == PacketType::NT_WT || type == PacketType::WT
                                 ? block_size
                                 : 0)
                    .burst(burst_size)
                    .type(type)
                    .build();
            XerxesLogger::debug()
                << name() << " issue packet " << pkt.id << " to " << qr.dpid
                << " at " << cur << std::endl;
            q.push(pkt);
            send_pkt(pkt);
            cur_cnt++;
            return true;
        } else {
            if (!ended) {
                ended = true;
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

    bool all_issued() { return hpa_gen_->eof(); }
    bool q_empty() { return q.empty(); }
};
} // namespace xerxes

#endif // XERXES_REQUESTER_HH
