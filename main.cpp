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
  auto hostq = [](int memcnt) {
    return memcnt == 1   ? 8
           : memcnt == 2 ? 20
           : memcnt == 4 ? 48
           : memcnt == 8 ? 128
                         : 16 * memcnt;
  };
  auto output = [](size_t burst, int memcnt) {
    return "output/bus_burst" + std::to_string(burst) + "_mem" +
           std::to_string(memcnt) + ".csv";
  };
  auto stats_out = [](size_t burst, int memcnt) {
    return "output/bus_burst" + std::to_string(burst) + "_mem" +
           std::to_string(memcnt) + ".txt";
  };
  size_t burst = 4;
  auto config = EpochConfig{0,
                            0,
                            0,
                            1,
                            burst,
                            false,
                            true,
                            15,
                            1,
                            32,
                            8 * 32,
                            40,
                            0,
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

  auto do_epoch = [&](size_t memcnt) {
    config.memcnt = memcnt;
    config.hostq = hostq(memcnt);
    config.cnt = 1000 * config.memcnt + config.hostq;
    config.output = output(burst, memcnt);
    config.stats_out = stats_out(burst, memcnt);
    std::cout << "Doing epoch [memcnt=" << memcnt << "]..." << std::flush;
    epoch(config);
    std::cout << "Done." << std::endl;
  };

  do_epoch(1);
  do_epoch(2);
  do_epoch(4);
  do_epoch(8);
  do_epoch(16);
  do_epoch(32);
  return 0;
}

void epoch(EpochConfig &config) {
  // Make simulation skeleton.
  auto sim = xerxes::Simulation{};

  // Make devices.
  auto host = xerxes::Host{sim.topology(), config.hostq, config.snoop,
                           config.cnt,     config.hostd, config.burst};
  auto snoop_evict = new xerxes::Snoop::FIFO{};
  auto snoop = xerxes::Snoop{sim.topology(), config.linen, config.assoc,
                             snoop_evict,    false,        "snoop0"};
  auto bus = xerxes::DuplexBus{sim.topology(), config.is_full, config.halft,
                               config.tpert,   config.bwidth,  config.framing};

  auto pkg0 = xerxes::Packaging{sim.topology(), config.package_num, "package0"};
  auto pkg1 = xerxes::Packaging{sim.topology(), config.package_num, "package1"};

  auto sw = xerxes::Switch{sim.topology(), config.switch_, "switch"};
  std::vector<xerxes::DRAMsim3Interface *> mems;
  std::vector<xerxes::BurstHandler *> bursts;
  for (int i = 0; i < config.memcnt; ++i) {
    bursts.push_back(
        new xerxes::BurstHandler{sim.topology(), "burst" + std::to_string(i)});
    mems.push_back(new xerxes::DRAMsim3Interface{
        sim.topology(), config.clock, config.proce,
        config.capa * (xerxes::Addr)(i), config.config, "output",
        "mem" + std::to_string(i)});
  }

  // Make topology by `add_dev()` and `add_edge()`.
  // Use `build_route()` to acctually build route table.
  // Use `set_entry()` to set the entry point of the simulation.
  sim.system()->add_dev(&host)->add_dev(&bus)->add_dev(&sw)->add_dev(&snoop);
  for (auto i = 0; i < config.memcnt; ++i) {
    sim.system()->add_dev(bursts[i]);
    sim.system()->add_dev(mems[i]);
  }
  sim.system()->add_dev(&pkg0)->add_dev(&pkg1);

  sim.topology()
      ->add_edge(host.id(), pkg0.id())
      ->add_edge(pkg0.id(), bus.id())
      ->add_edge(bus.id(), pkg1.id())
      ->add_edge(pkg1.id(), sw.id());
  for (auto i = 0; i < config.memcnt; ++i) {
    sim.topology()->add_edge(sw.id(), bursts[i]->id());
    sim.topology()->add_edge(bursts[i]->id(), mems[i]->id());
  }
  sim.topology()->build_route();
  sim.set_entry(host.id());

  // Use `log_route()` to log the route table.
  sim.topology()->log_route(xerxes::Logger::debug());

  // Build a notifier, triggered when a device is receiving a packet.
  auto notifier_func = [&](void *topo_id) {
    auto id = (xerxes::TopoID)topo_id;
    auto dev = sim.system()->find_dev(id);
    dev->transit();
  };

  // Build a packet logger, triggered when a packet calls `log_stats()`.
  std::fstream fout = std::fstream(config.output, std::ios::out);
  fout << "id,type,mem_id,addr,sent,arrive,"
       << "device_process_time,"
       << "dram_q_time,"
       << "dram_time,"
       << "framing_time,"
       << "packaging_delay,"
       << "wait_burst,"
       << "bus_q_time,"
       << "bus_time,"
       << "switch_q_time,"
       << "switch_time,"
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

    xerxes::Logger::info() << pkt.id << "," << xerxes::TypeName::of(pkt.type);
    xerxes::Logger::info() << "," << pkt.src << "," << pkt.addr << ","
                           << pkt.sent << "," << pkt.arrive;
    log_stat(xerxes::DEVICE_PROCESS_TIME);
    log_stat(xerxes::DRAM_INTERFACE_QUEUING_DELAY);
    log_stat(xerxes::DRAM_TIME);
    log_stat(xerxes::FRAMING_TIME);
    log_stat(xerxes::PACKAGING_DELAY);
    log_stat(xerxes::WAIT_ALL_BURST);
    log_stat(xerxes::BUS_QUEUE_DELAY);
    log_stat(xerxes::BUS_TIME);
    log_stat(xerxes::SWITCH_QUEUE_DELAY);
    log_stat(xerxes::SWITCH_TIME);
    log_stat(xerxes::SNOOP_EVICT_DELAY);
    log_stat(xerxes::HOST_INV_DELAY);
    xerxes::Logger::info() << "," << pkt.arrive - pkt.sent << std::endl;
  };

  // Initialize the global utils, including notifier, logger stream, log level,
  // pkt logger.
  xerxes::global_init(notifier_func, fout, config.loglevel, pkt_logger);

  // Add end points to the host.
  for (size_t i = 0; i < mems.size(); ++i) {
    host.add_end_point(mems[i]->id(), config.capa * i, config.capa * (i + 1),
                       config.random);
  }

  // Add topology to the switch.
  sw.add_upstream(host.id());
  for (auto mem : mems) {
    sw.add_downstream(mem->id());
  }

  // Add topology to packaging.
  pkg0.add_upstream(host.id());
  pkg1.add_upstream(sw.id());

  auto &notifier = *xerxes::Notifier::glb();
  auto issue_cnt = 0;
  auto ratio = config.ratio;

  while (!host.all_issued()) {
    bool res = false;
    if (ratio == 0) {
      res = host.step(xerxes::PacketType::RD);
    } else if (ratio == -1) {
      res = host.step(xerxes::PacketType::WT);
    } else {
      res = host.step((issue_cnt / 4) % (ratio + 1) == ratio
                          ? xerxes::PacketType::WT
                          : xerxes::PacketType::RD);
    }
    issue_cnt += (int)(res);
    notifier.step();
    if (!res) {
      for (int i = 0; i < config.grain; ++i) {
        for (auto mem : mems) {
          mem->clock();
        }
      }
    }
  }
  auto clock_cnt = 0;
  while (!host.q_empty() && (clock_cnt++) < config.max_clock) {
    notifier.step();
    for (int i = 0; i < config.grain; ++i) {
      for (auto mem : mems) {
        mem->clock();
      }
    }
  }

  // Log the statistics of the devices.
  std::fstream stats_out = std::fstream(config.stats_out, std::ios::out);
  host.log_stats(stats_out);
  bus.log_stats(stats_out);
}
