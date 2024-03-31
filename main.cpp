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
  size_t concentration;
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
  size_t host_inv_time;
  size_t linen;
  size_t assoc;
  size_t burst_inv;
  std::string evict_policy;
  // Epoch def.
  xerxes::LogLevel loglevel;
  int ratio;
  int memcnt;
  std::string output;
  std::string stats_out;
};

void epoch(EpochConfig &);

int main(int argc, char *argv[]) {
  auto config = EpochConfig{2000,
                            32,
                            0,
                            1,
                            1,
                            true,
                            1,
                            true,
                            15,
                            1,
                            256,
                            8 * 256,
                            40,
                            6,
                            (64 * 600),
                            1,
                            40,
                            10,
                            10000,
                            "output/dram.ini",
                            80,
                            128,
                            128,
                            2,
                            "FIFO",
                            xerxes::LogLevel::INFO,
                            0,
                            1,
                            "output/try.csv",
                            "output/try.txt"};

  std::vector<size_t> concentration = {(size_t)atoi(argv[1])};
  for (auto con : concentration) {
    config.concentration = con;
    config.output = "output/burst_inv_concen_" + std::to_string(con) + ".csv";
    config.stats_out =
        "output/burst_inv_concen_" + std::to_string(con) + ".txt";
    std::cout << "Epoch [concentration = " << con << "]..." << std::flush;
    epoch(config);
    std::cout << "done." << std::endl;
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
  auto host_cnt = config.memcnt + 1;

  // Make devices.
  std::vector<xerxes::Host *> hosts = {};
  std::vector<xerxes::Snoop *> snoops = {};
  std::vector<xerxes::DRAMsim3Interface *> mems = {};
  for (int i = 0; i < host_cnt; ++i) {
    hosts.push_back(new xerxes::Host{sim.topology(), config.hostq,
                                     config.host_inv_time, config.cnt,
                                     config.hostd, config.burst});
  }
  for (int i = 0; i < config.memcnt; ++i) {
    snoops.push_back(new xerxes::Snoop{sim.topology(), config.linen,
                                       config.assoc, config.burst_inv,
                                       get_evict(config.evict_policy), true});
    mems.push_back(new xerxes::DRAMsim3Interface{sim.topology(), config.clock,
                                                 config.proce, 0, config.config,
                                                 "output", "mem"});
  }

  // Make topology by `add_dev()` and `add_edge()`.
  // Use `build_route()` to acctually build route table.
  // Use `set_entry()` to set the entry point of the simulation.
  for (int i = 0; i < host_cnt; ++i) {
    sim.system()->add_dev(hosts[i]);
  }
  for (int i = 0; i < config.memcnt; ++i) {
    sim.system()->add_dev(snoops[i]);
    sim.system()->add_dev(mems[i]);
  }
  for (int i = 0; i < config.memcnt; ++i) {
    for (int j = 0; j < host_cnt; ++j) {
      sim.topology()->add_edge(hosts[j]->id(), snoops[i]->id());
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
  for (int i = 0; i < host_cnt; ++i) {
    for (int j = 0; j < config.memcnt; ++j) {
      hosts[i]->add_end_point(mems[j]->id(), 0,
                              i == 1 ? config.capa / config.concentration
                                     : config.capa,
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
    for (int i = 0; i < host_cnt; ++i) {
      res[i] = true;
    }
    while (true) {
      int failed_cnt = 0;
      for (int i = 0; i < host_cnt; ++i) {
        auto h = (i + first_host) % host_cnt;
        if (res[h]) {
          res[h] = hosts[h]->step(type(issue_cnts[h]));
          issue_cnts[h] += (int)(res[h]);
        }
        if (!res[h]) {
          failed_cnt++;
        }
      }
      first_host = (first_host + 1) % host_cnt;
      if (failed_cnt == host_cnt) {
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
  while ((int)emptied.size() < host_cnt && (clock_cnt++) < config.max_clock) {
    notifier.step();
    for (int i = 0; i < config.grain; ++i)
      for (int j = 0; j < config.memcnt; ++j)
        mems[j]->clock();

    for (int j = 0; j < host_cnt; ++j)
      if (hosts[j]->q_empty() && !emptied.count(j))
        emptied.insert(j);
  }

  // Log the statistics of the devices.
  std::fstream stats_out = std::fstream(config.stats_out, std::ios::out);
  // Use `log_route()` to log the route table.
  // sim.topology()->log_route(stats_out);
  for (int i = 0; i < host_cnt; ++i) {
    hosts[i]->log_stats(stats_out);
  }
  for (int i = 0; i < config.memcnt; ++i) {
    snoops[i]->log_stats(stats_out);
  }
  // bus.log_stats(stats_out);
}
