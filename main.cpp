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
  // Epoch def.
  xerxes::LogLevel loglevel;
  int ratio;
  int memcnt;
  std::string output;
  std::string stats_out;
};

void epoch(EpochConfig &);

int main() {
  auto config = EpochConfig{2000,
                            8,
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
                            16,
                            xerxes::LogLevel::INFO,
                            0,
                            0,
                            "",
                            ""};

  std::vector<size_t> epoch_assoc = {1, 4, 8, 16, 32, 64, 128};
  for (size_t i = 0; i < epoch_assoc.size(); ++i) {
    config.assoc = epoch_assoc[i];
    config.output =
        "output/coh_assoc" + std::to_string(epoch_assoc[i]) + ".csv";
    config.stats_out =
        "output/coh_assoc" + std::to_string(epoch_assoc[i]) + ".txt";
    std::cout << "Epoch " << i << "..." << std::flush;
    epoch(config);
    std::cout << "done." << std::endl;
  }

  return 0;
}

void epoch(EpochConfig &config) {
  // Make simulation skeleton.
  auto sim = xerxes::Simulation{};

  // Make devices.
  auto host0 = xerxes::Host{sim.topology(), config.hostq, config.snoop,
                            config.cnt,     config.hostd, config.burst};
  auto host1 = xerxes::Host{sim.topology(), config.hostq, config.snoop,
                            config.cnt,     config.hostd, config.burst};
  auto snoop = xerxes::Snoop{sim.topology(), config.linen, config.assoc,
                             new xerxes::Snoop::FIFO{}};
  auto mem =
      xerxes::DRAMsim3Interface{sim.topology(), config.clock, config.proce, 0,
                                config.config,  "output",     "mem"};

  // Make topology by `add_dev()` and `add_edge()`.
  // Use `build_route()` to acctually build route table.
  // Use `set_entry()` to set the entry point of the simulation.
  sim.system()->add_dev(&host0)->add_dev(&host1)->add_dev(&snoop)->add_dev(
      &mem);

  sim.topology()
      ->add_edge(host0.id(), snoop.id())
      ->add_edge(host1.id(), snoop.id())
      ->add_edge(snoop.id(), mem.id());
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
  host0.add_end_point(mem.id(), 0, config.capa, true);
  host1.add_end_point(mem.id(), 0, config.capa, false);

  auto &notifier = *xerxes::Notifier::glb();
  auto issue_cnt0 = 0, issue_cnt1 = 0;
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

  int first_host = 0;
  while (!host0.all_issued() || !host1.all_issued()) {
    bool res0 = true, res1 = true;
    while (res0 || res1) {
      if (first_host == 0) {
        if (res0) {
          res0 = host0.step(type(issue_cnt0));
          issue_cnt0 += (int)(res0);
        }
        if (res1) {
          res1 = host1.step(type(issue_cnt1));
          issue_cnt1 += (int)(res1);
        }
        first_host = 1;
      } else {
        if (res1) {
          res1 = host1.step(type(issue_cnt1));
          issue_cnt1 += (int)(res1);
        }
        if (res0) {
          res0 = host0.step(type(issue_cnt0));
          issue_cnt0 += (int)(res0);
        }
        first_host = 0;
      }
    }
    auto rem = notifier.step();
    if (!res0 || !res1 || rem == 0) {
      for (int i = 0; i < config.grain; ++i) {
        mem.clock();
      }
    }
  }
  auto clock_cnt = 0;
  while (!host0.q_empty() && !host1.q_empty() &&
         (clock_cnt++) < config.max_clock) {
    notifier.step();
    for (int i = 0; i < config.grain; ++i) {
      mem.clock();
    }
  }

  // Log the statistics of the devices.
  std::fstream stats_out = std::fstream(config.stats_out, std::ios::out);
  // Use `log_route()` to log the route table.
  // sim.topology()->log_route(stats_out);
  host0.log_stats(stats_out);
  host1.log_stats(stats_out);
  // bus.log_stats(stats_out);
}
