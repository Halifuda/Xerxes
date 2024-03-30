#include "def.hpp"
#include "xerxes_core.hpp"

#include <fstream>
#include <iostream>

struct EpochConfig {
  // Issueing def.
  size_t cnt;
  size_t hostq;
  size_t hostd;
  size_t package_num;
  size_t burst;
  bool random;
  // Bus def.
  bool is_full;
  size_t halft;
  size_t tpert;
  size_t byte_width;
  size_t bwidth;
  size_t framing;
  // Switch def.
  size_t switch_;
  // DRAM def.
  size_t capa;
  size_t clock;
  size_t proce;
  int grain;
  int max_clock;
  std::string config;
  // Snoop def.
  size_t snoop;
  size_t linen;
  size_t assoc;
  std::string evict_policy;
  // Epoch def.
  xerxes::LogLevel loglevel;
  int ratio;
  int memcnt;
  std::string output;
  std::string stats_out;
  std::pair<double, double> avg_bw__and__avg_conflict_cnt;
};

void epoch(EpochConfig &);

int main() {
  auto config = EpochConfig{2000,
                            20,
                            0,
                            1,
                            1,
                            true,
                            true,
                            15,
                            1,
                            256,
                            8 * 256,
                            40,
                            6,
                            (64 * 500),
                            1,
                            40,
                            10,
                            10000,
                            "output/dram.ini",
                            20,
                            128,
                            128,
                            "",
                            xerxes::LogLevel::INFO,
                            0,
                            2,
                            "/dev/null",
                            "/dev/null"};

  std::vector<int> memcnts = {2, 4, 8, 16};
  std::vector<std::string> evictions = {"FIFO", "LIFO", "LRU", "MRU"};
  std::vector<std::pair<double, double>> avg_bw__and__avg_conflict_cnt;
  for (int m : memcnts) {
    avg_bw__and__avg_conflict_cnt.clear();
    config.memcnt = m;
    for (size_t i = 0; i < evictions.size(); ++i) {
      config.evict_policy = evictions[i];
      // config.output = "output/coh_mem" + std::to_string(m) + "_evict_" +
      //                 evictions[i] + ".csv";
      // config.stats_out = "output/coh_mem" + std::to_string(m) + "_evict_" +
      //                    evictions[i] + ".txt";
      std::cout << "Epoch [m" << m << ", e=" << evictions[i] << "]..."
                << std::flush;
      epoch(config);
      std::cout << "done." << std::endl;
      avg_bw__and__avg_conflict_cnt.push_back(
          config.avg_bw__and__avg_conflict_cnt);
    }
    std::fstream stats_out = std::fstream(
        "output/evictions_mem" + std::to_string(m) + ".csv", std::ios::out);
    stats_out << "Eviction policy,Average bandwidth (GB/s),Average conflict "
                 "count"
              << std::endl;
    for (size_t i = 0; i < evictions.size(); ++i) {
      stats_out << evictions[i] << "," << avg_bw__and__avg_conflict_cnt[i].first
                << "," << avg_bw__and__avg_conflict_cnt[i].second << std::endl;
    }
    stats_out.close();
  }

  return 0;
}

void epoch(EpochConfig &config) {
  // Make simulation skeleton.
  auto sim = xerxes::Simulation{};

  auto get_evict = [](std::string name) {
    if (name == "FIFO") {
      return (xerxes::Snoop::SnoopEviction *)new xerxes::Snoop::FIFO{};
    } else if (name == "LIFO") {
      return (xerxes::Snoop::SnoopEviction *)new xerxes::Snoop::LIFO{};
    } else if (name == "LRU") {
      return (xerxes::Snoop::SnoopEviction *)new xerxes::Snoop::LRU{};
    } else if (name == "MRU") {
      return (xerxes::Snoop::SnoopEviction *)new xerxes::Snoop::MRU{};
    } else if (name == "Random") {
      return (xerxes::Snoop::SnoopEviction *)new xerxes::Snoop::Random{};
    } else {
      return (xerxes::Snoop::SnoopEviction *)new xerxes::Snoop::FIFO{};
    }
  };

  // Make devices.
  std::vector<xerxes::Host *> hosts = {};
  std::vector<xerxes::Snoop *> snoops = {};
  std::vector<xerxes::DRAMsim3Interface *> mems = {};
  for (int i = 0; i < config.memcnt; ++i) {
    hosts.push_back(new xerxes::Host{sim.topology(), config.hostq, config.snoop,
                                     config.cnt, config.hostd, config.burst});
    snoops.push_back(new xerxes::Snoop{sim.topology(), config.linen,
                                       config.assoc,
                                       get_evict(config.evict_policy), true});
    mems.push_back(new xerxes::DRAMsim3Interface{sim.topology(), config.clock,
                                                 config.proce, 0, config.config,
                                                 "output", "mem"});
  }

  // Make topology by `add_dev()` and `add_edge()`.
  // Use `build_route()` to acctually build route table.
  // Use `set_entry()` to set the entry point of the simulation.
  for (int i = 0; i < config.memcnt; ++i) {
    sim.system()->add_dev(hosts[i]);
    sim.system()->add_dev(snoops[i]);
    sim.system()->add_dev(mems[i]);
  }
  for (int i = 0; i < config.memcnt; ++i) {
    for (int j = 0; j < config.memcnt; ++j) {
      sim.topology()->add_edge(hosts[i]->id(), snoops[j]->id());
    }
    sim.topology()->add_edge(snoops[i]->id(), mems[i]->id());
  }
  sim.topology()->build_route();

  // Build a notifier, triggered when a device is receiving a packet.
  auto notifier_func = [&](void *topo_id) {
    auto id = (xerxes::TopoID)topo_id;
    auto dev = sim.system()->find_dev(id);
    dev->transit();
  };

  // Build a packet logger, triggered when a packet calls `log_stats()`.
  std::fstream fout = std::fstream(config.output, std::ios::out);
  fout << "id,host,type,mem_id,addr,sent,arrive,"
       << "device_process_time,"
       << "dram_q_time,"
       << "dram_time,"
       // << "framing_time,"
       // << "packaging_delay,"
       // << "wait_burst,"
       // << "bus_q_time,"
       // << "bus_time,"
       // << "switch_q_time,"
       // << "switch_time,"
       << "snoop_evict_time,"
       << "host_inv_time,"
       << "total_time" << std::endl;
  auto pkt_logger = [&](const xerxes::Packet &pkt) {
    auto log_stat = [&](xerxes::NormalStatType stat) {
      if (pkt.has_stat(stat) > 0) {
        xerxes::Logger::info() << "," << pkt.get_stat(stat);
      } else {
        xerxes::Logger::info() << ",0";
      }
    };

    xerxes::Logger::info() << pkt.id << "," << pkt.dst << ","
                           << xerxes::TypeName::of(pkt.type) << "," << pkt.src
                           << "," << pkt.addr << "," << pkt.sent << ","
                           << pkt.arrive;
    log_stat(xerxes::DEVICE_PROCESS_TIME);
    log_stat(xerxes::DRAM_INTERFACE_QUEUING_DELAY);
    log_stat(xerxes::DRAM_TIME);
    // log_stat(xerxes::FRAMING_TIME);
    // log_stat(xerxes::PACKAGING_DELAY);
    // log_stat(xerxes::WAIT_ALL_BURST);
    // log_stat(xerxes::BUS_QUEUE_DELAY);
    // log_stat(xerxes::BUS_TIME);
    // log_stat(xerxes::SWITCH_QUEUE_DELAY);
    // log_stat(xerxes::SWITCH_TIME);
    log_stat(xerxes::SNOOP_EVICT_DELAY);
    log_stat(xerxes::HOST_INV_DELAY);
    xerxes::Logger::info() << "," << pkt.arrive - pkt.sent << std::endl;
  };

  // Initialize the global utils, including notifier, logger stream, log level,
  // pkt logger.
  xerxes::global_init(notifier_func, fout, config.loglevel, pkt_logger);

  // Add end points to the host.
  for (int i = 0; i < config.memcnt; ++i) {
    for (int j = 0; j < config.memcnt; ++j) {
      hosts[i]->add_end_point(mems[j]->id(), 0, config.capa,
                              i == j ? false : true);
    }
  }

  auto &notifier = *xerxes::Notifier::glb();
  std::vector<size_t> issue_cnts = std::vector<size_t>(config.memcnt, 0);
  auto ratio = config.ratio;
  auto type = [&](int issue_cnt) {
    if (ratio == 0) {
      return xerxes::PacketType::RD;
    } else if (ratio == -1) {
      return xerxes::PacketType::WT;
    } else {
      return (issue_cnt / 4) % (ratio + 1) == ratio ? xerxes::PacketType::WT
                                                    : xerxes::PacketType::RD;
    }
  };

  size_t first_host = 0;
  std::vector<bool> res = std::vector<bool>(config.memcnt, true);
  while (true) {
    bool finished = true;
    for (auto host : hosts) {
      if (!host->all_issued()) {
        finished = false;
        break;
      }
    }
    if (finished) {
      break;
    }
    for (int i = 0; i < config.memcnt; ++i) {
      res[i] = true;
    }
    while (true) {
      int failed_cnt = 0;
      for (int i = 0; i < config.memcnt; ++i) {
        auto h = (i + first_host) % config.memcnt;
        if (res[h]) {
          res[h] = hosts[h]->step(type(issue_cnts[h]));
          issue_cnts[h] += (int)(res[h]);
        }
        if (!res[h]) {
          failed_cnt++;
        }
      }
      first_host = (first_host + 1) % config.memcnt;
      if (failed_cnt == config.memcnt) {
        break;
      }
    }
    auto rem = notifier.step();
    if (rem == 0) {
      for (int i = 0; i < config.grain; ++i) {
        for (int j = 0; j < config.memcnt; ++j) {
          mems[j]->clock();
        }
      }
    }
  }
  auto clock_cnt = 0;
  std::set<int> emptied = {};
  while ((int)emptied.size() < config.memcnt &&
         (clock_cnt++) < config.max_clock) {
    notifier.step();
    for (int i = 0; i < config.grain; ++i) {
      for (int j = 0; j < config.memcnt; ++j) {
        mems[j]->clock();
        if (hosts[j]->q_empty() && !emptied.count(j)) {
          emptied.insert(j);
        }
      }
    }
  }

  // Log the statistics of the devices.
  std::fstream stats_out = std::fstream(config.stats_out, std::ios::out);
  // Use `log_route()` to log the route table.
  // sim.topology()->log_route(stats_out);
  stats_out << "Host average bandwidth (GB/s): " << std::endl;
  double total_bw = 0;
  for (int i = 0; i < config.memcnt; ++i) {
    stats_out << " * " << hosts[i]->name() << ": " << hosts[i]->avg_bw()
              << std::endl;
    total_bw += hosts[i]->avg_bw();
  }
  stats_out << " * Total: " << total_bw << std::endl;

  stats_out << "Snoop average conflict count: " << std::endl;
  double total_conflict = 0;
  for (int i = 0; i < config.memcnt; ++i) {
    stats_out << " * " << snoops[i]->name() << ": "
              << snoops[i]->avg_conflict_cnt() << std::endl;
    total_conflict += snoops[i]->avg_conflict_cnt();
  }
  stats_out << " * Total: " << total_conflict << std::endl;
  // bus.log_stats(stats_out);

  config.avg_bw__and__avg_conflict_cnt =
      std::make_pair(total_bw, total_conflict);
}
