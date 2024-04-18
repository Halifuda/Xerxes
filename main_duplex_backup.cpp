#include "def.hpp"
#include "xerxes_core.hpp"

#include <fstream>
#include <iomanip>
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
  size_t frame_size;
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
  // Epoch def.
  xerxes::LogLevel loglevel;
  double ratio;
  int hostcnt;
  int memcnt;
  std::string output;
  std::string stats_out;

  double bw = 0;
  double util = 0;
  double eff = 0;
};

void epoch(EpochConfig &);

int main(int argc, char *argv[]) {
  const size_t tick = 1000;
  auto config = EpochConfig{
      1000,     20,       0,        1,           1,
      true,     true,     1000,     20,          1,
      8,        64,       0 * tick, 0 * tick,    (64 * 600),
      1 * tick, 0 * tick, 10,       10000,       "output/dram.ini",
      80000,    128,      128,      1,           xerxes::LogLevel::INFO,
      0,        1,        8,        "/dev/null", "/dev/null"};

  config.frame_size = atoi(argv[1]);
  config.memcnt = atoi(argv[2]);
  config.ratio = atof(argv[3]);
  config.cnt = config.memcnt * 3000;
  config.hostq = config.memcnt * 16;

  auto prefix = std::string("output/mem") + std::to_string(config.memcnt) +
                "_ratio" + std::to_string(config.ratio);
  // config.output = prefix + ".csv";
  // config.stats_out = prefix + "_stats.csv";

  std::cout << "Epoch [frame=" << config.frame_size << ", mem=" << config.memcnt
            << ", ratio=" << config.ratio << "]..." << std::flush;
  epoch(config);
  auto bw = config.bw * tick;
  auto util = config.util;
  auto eff = config.eff;
  std::cout << "done." << std::endl;

  auto sum_output = std::fstream(
      "output/summary_framesize=" + std::to_string(config.frame_size) + ".csv",
      std::ios::app);
  sum_output << config.memcnt << "," << config.ratio << "," << bw << "," << util
             << "," << eff << std::endl;

  return 0;
}

void epoch(EpochConfig &config) {
  // Make simulation skeleton.
  auto sim = xerxes::Simulation{};

  // Make devices.
  std::vector<xerxes::Requester *> hosts = {};
  std::vector<xerxes::DRAMsim3Interface *> mems = {};
  for (int i = 0; i < config.hostcnt; ++i) {
    hosts.push_back(new xerxes::Requester{
        sim.topology(), config.hostq, 128, config.host_inv_time, config.cnt,
        config.hostd, config.burst, new xerxes::Requester::Random{}});
  }
  for (int i = 0; i < config.memcnt; ++i) {
    mems.push_back(new xerxes::DRAMsim3Interface{sim.topology(), config.clock,
                                                 config.proce, 0, config.config,
                                                 "output", "mem"});
  }
  auto bus = new xerxes::DuplexBus{
      sim.topology(), config.is_full, config.halft,      config.tpert,
      config.bwidth,  config.framing, config.frame_size, "bus"};
  auto swtch0 = new xerxes::Switch{sim.topology(), config.switch_delay};
  auto swtch1 = new xerxes::Switch{sim.topology(), config.switch_delay};

  auto pkg0 = new xerxes::Packing{sim.topology(), config.package_num};
  auto pkg1 = new xerxes::Packing{sim.topology(), config.package_num};

  // Make topology by `add_dev()` and `add_edge()`.
  // Use `build_route()` to acctually build route table.
  // Use `set_entry()` to set the entry point of the simulation.
  for (int i = 0; i < config.hostcnt; ++i) {
    sim.system()->add_dev(hosts[i]);
  }
  for (int i = 0; i < config.memcnt; ++i) {
    sim.system()->add_dev(mems[i]);
  }
  sim.system()->add_dev(bus)->add_dev(swtch0)->add_dev(swtch1);
  sim.system()->add_dev(pkg0)->add_dev(pkg1);
  pkg0->add_upstream(swtch0->id());
  pkg1->add_upstream(swtch1->id());
  for (int i = 0; i < config.hostcnt; ++i) {
    sim.topology()->add_edge(hosts[i]->id(), swtch0->id());
  }
  sim.topology()
      ->add_edge(swtch0->id(), pkg0->id())
      ->add_edge(pkg0->id(), bus->id())
      ->add_edge(bus->id(), pkg1->id())
      ->add_edge(pkg1->id(), swtch1->id());
  for (int i = 0; i < config.memcnt; ++i) {
    sim.topology()->add_edge(swtch1->id(), mems[i]->id());
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
       << "framing_time,"
       // << "packaging_delay,"
       // << "wait_burst,"
       << "bus_q_time,"
       << "bus_time,"
       << "switch_q_time,"
       << "switch_time,"
       // << "snoop_evict_time,"
       // << "host_inv_time,"
       << "total_time" << std::endl;
  auto pkt_logger = [&](const xerxes::Packet &pkt) {
    auto log_stat = [&](xerxes::NormalStatType stat) {
      if (pkt.has_stat(stat) > 0) {
        xerxes::Logger::info()
            << std::fixed << std::setprecision(0) << "," << pkt.get_stat(stat);
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
    log_stat(xerxes::FRAMING_TIME);
    // log_stat(xerxes::PACKAGING_DELAY);
    // log_stat(xerxes::WAIT_ALL_BURST);
    log_stat(xerxes::BUS_QUEUE_DELAY);
    log_stat(xerxes::BUS_TIME);
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
  for (int i = 0; i < config.hostcnt; ++i) {
    for (int j = 0; j < config.memcnt; ++j) {
      hosts[i]->add_end_point(mems[j]->id(), 0, config.capa, config.ratio);
    }
  }

  auto &notifier = *xerxes::Notifier::glb();
  std::vector<size_t> issue_cnts = std::vector<size_t>(config.memcnt, 0);

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
    for (int i = 0; i < config.hostcnt; ++i) {
      res[i] = true;
    }
    while (true) {
      int failed_cnt = 0;
      for (int i = 0; i < config.hostcnt; ++i) {
        auto h = (i + first_host) % config.hostcnt;
        if (res[h]) {
          res[h] = hosts[h]->step(false);
          issue_cnts[h] += (int)(res[h]);
        }
        if (!res[h]) {
          failed_cnt++;
        }
      }
      first_host = (first_host + 1) % config.hostcnt;
      if (failed_cnt == config.hostcnt) {
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
  while ((int)emptied.size() < config.hostcnt &&
         (clock_cnt++) < config.max_clock) {
    notifier.step();
    for (int i = 0; i < config.grain; ++i)
      for (int j = 0; j < config.memcnt; ++j)
        mems[j]->clock();

    for (int j = 0; j < config.hostcnt; ++j)
      if (hosts[j]->q_empty() && !emptied.count(j))
        emptied.insert(j);
  }

  // Log the statistics of the devices.
  std::fstream stats_out = std::fstream(config.stats_out, std::ios::out);
  // Use `log_route()` to log the route table.
  // sim.topology()->log_route(stats_out);
  double agg_bw = 0;
  stats_out << "End point,Bandwidth,Latency,Bus queuing" << std::endl;
  for (int i = 0; i < config.hostcnt; ++i) {
    agg_bw += hosts[i]->log_stats_csv(stats_out);
  }
  bus->log_stats(stats_out);
  config.bw = agg_bw;
  config.util = bus->avg_utilization();
  config.eff = bus->efficiency();
}
