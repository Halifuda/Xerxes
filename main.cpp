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
  size_t switch_delay;
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
  std::string swtopo;
  // Epoch def.
  xerxes::LogLevel loglevel;
  int ratio;
  int memcnt;
  std::string output;
  std::string stats_out;
};

void epoch(EpochConfig &);

int main(int argc, char *argv[]) {
  auto config = EpochConfig{2000,        32,
                            0,           1,
                            1,           true,
                            1,           true,
                            15,          1,
                            256,         8 * 256,
                            40,          6,
                            (64 * 600),  1,
                            40,          10,
                            10000,       "output/dram.ini",
                            80,          128,
                            128,         2,
                            "tree",      xerxes::LogLevel::INFO,
                            0,           4,
                            "/dev/null", "output/try_stats.csv"};

  config.swtopo = argv[1];
  config.memcnt = atoi(argv[2]);
  // config.output = "output/swtopo_" + config.swtopo + "_mem" +
  //                 std::to_string(config.memcnt) + ".csv";
  config.stats_out = "output/swtopo_" + config.swtopo + "_mem" +
                     std::to_string(config.memcnt) + "_stats.csv";

  std::cout << "Epoch [" << config.swtopo << ", mem=" << config.memcnt << "]..."
            << std::flush;
  epoch(config);
  std::cout << "done." << std::endl;

  return 0;
}

void epoch(EpochConfig &config) {
  // Make simulation skeleton.
  auto sim = xerxes::Simulation{};
  auto host_cnt = config.memcnt;

  // Make devices.
  std::vector<xerxes::Host *> hosts = {};
  std::vector<xerxes::DRAMsim3Interface *> mems = {};
  for (int i = 0; i < host_cnt; ++i) {
    hosts.push_back(new xerxes::Host{sim.topology(), config.hostq,
                                     config.host_inv_time, config.cnt,
                                     config.hostd, config.burst});
  }
  for (int i = 0; i < config.memcnt; ++i) {
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
    sim.system()->add_dev(mems[i]);
  }

  std::vector<xerxes::Switch *> switches = {};
  if (config.swtopo == "chain") {
    for (int i = 0; i < host_cnt; ++i) {
      auto *sw = new xerxes::Switch{sim.topology(), config.switch_delay, "sw"};
      sim.system()->add_dev(sw);
      sim.topology()
          ->add_edge(hosts[i]->id(), sw->id())
          ->add_edge(mems[i]->id(), sw->id());
      switches.push_back(sw);
    }
    for (int i = 0; i < host_cnt - 1; ++i) {
      sim.topology()->add_edge(switches[i]->id(), switches[i + 1]->id());
    }
  } else if (config.swtopo == "tree") {
    for (int i = 0; i < host_cnt; ++i) {
      auto *sw = new xerxes::Switch{sim.topology(), config.switch_delay, "sw"};
      sim.system()->add_dev(sw);
      sim.topology()
          ->add_edge(hosts[i]->id(), sw->id())
          ->add_edge(mems[i]->id(), sw->id());
      switches.push_back(sw);
    }
    auto cnt = host_cnt;
    size_t i = 0;
    while (cnt > 1) {
      auto *sw = new xerxes::Switch{sim.topology(), config.switch_delay, "sw"};
      sim.system()->add_dev(sw);
      sim.topology()
          ->add_edge(switches[i]->id(), sw->id())
          ->add_edge(switches[i + 1]->id(), sw->id());
      switches.push_back(sw);
      cnt--, i += 2;
    }
  } else if (config.swtopo == "ring") {
    for (int i = 0; i < host_cnt; ++i) {
      auto *sw = new xerxes::Switch{sim.topology(), config.switch_delay, "sw"};
      sim.system()->add_dev(sw);
      sim.topology()
          ->add_edge(hosts[i]->id(), sw->id())
          ->add_edge(mems[i]->id(), sw->id());
      switches.push_back(sw);
    }
    for (int i = 0; i < host_cnt; ++i) {
      sim.topology()->add_edge(switches[i]->id(),
                               switches[(i + 1) % host_cnt]->id());
    }
  } else if (config.swtopo == "star") {
    // Each low-level switch connected to one host and one memory, and all
    // low-level switches connected to a top switch.
    auto *top_sw =
        new xerxes::Switch{sim.topology(), config.switch_delay, "top_sw"};
    sim.system()->add_dev(top_sw);
    for (int i = 0; i < host_cnt; ++i) {
      auto *sw = new xerxes::Switch{sim.topology(), config.switch_delay, "sw"};
      sim.system()->add_dev(sw);
      sim.topology()
          ->add_edge(hosts[i]->id(), sw->id())
          ->add_edge(mems[i]->id(), sw->id());
      sim.topology()->add_edge(sw->id(), top_sw->id());
      switches.push_back(sw);
    }
    switches.push_back(top_sw);
  } else if (config.swtopo == "full") {
    // Each low-level switch connected to one host and one memory, and all
    // low-level switches connected to all other low-level switches.
    for (int i = 0; i < host_cnt; ++i) {
      auto *sw = new xerxes::Switch{sim.topology(), config.switch_delay, "sw"};
      sim.system()->add_dev(sw);
      sim.topology()
          ->add_edge(hosts[i]->id(), sw->id())
          ->add_edge(mems[i]->id(), sw->id());
      switches.push_back(sw);
    }
    for (int i = 0; i < host_cnt; ++i) {
      for (int j = i + 1; j < host_cnt; ++j)
        sim.topology()->add_edge(switches[i]->id(), switches[j]->id());
    }
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
       << "switch_q_time,"
       << "switch_time,"
       // << "snoop_evict_time,"
       // << "host_inv_time,"
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
    log_stat(xerxes::SWITCH_QUEUE_DELAY);
    log_stat(xerxes::SWITCH_TIME);
    // log_stat(xerxes::SNOOP_EVICT_DELAY);
    // log_stat(xerxes::HOST_INV_DELAY);
    xerxes::Logger::info() << "," << pkt.arrive - pkt.sent << std::endl;
  };

  // Initialize the global utils, including notifier, logger stream, log level,
  // pkt logger.
  xerxes::global_init(notifier_func, fout, config.loglevel, pkt_logger);

  // Add end points to the host.
  for (int i = 0; i < host_cnt; ++i) {
    for (int j = 0; j < config.memcnt; ++j) {
      hosts[i]->add_end_point(mems[j]->id(), 0, config.capa, config.random);
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
      return issue_cnt % (ratio + 1) == ratio ? xerxes::PacketType::WT
                                              : xerxes::PacketType::RD;
    }
  };

  size_t first_host = 0;
  size_t force_stop = 0;
  std::vector<bool> res = std::vector<bool>(config.memcnt, true);
  while (true && force_stop < 1000000) {
    force_stop++;
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
  size_t port_num = 0;
  for (auto &sw : switches) {
    port_num += sw->port_num();
  }
  stats_out << "End point,Bandwidth,Latency,Switch queuing,Switch time"
            << std::endl;
  stats_out << "Switch number: " << switches.size() << std::endl;
  stats_out << "Port number: " << port_num << std::endl;
  for (int i = 0; i < host_cnt; ++i) {
    hosts[i]->log_stats_1(stats_out);
  }
  // bus.log_stats(stats_out);
}
