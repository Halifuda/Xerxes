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
  auto config = EpochConfig{4100,
                            128,
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
                            "output/chain_switch.csv",
                            "output/chain_switch.txt"};

  epoch(config);
  return 0;
}

void epoch(EpochConfig &config) {
  config.memcnt = 4;
  // Make simulation skeleton.
  auto sim = xerxes::Simulation{};

  // Make devices.
  auto host = xerxes::Host{sim.topology(), config.hostq, config.snoop,
                           config.cnt,     config.hostd, config.burst};
  auto bus = xerxes::DuplexBus{sim.topology(), config.is_full, config.halft,
                               config.tpert,   config.bwidth,  config.framing};
  auto sw0 = xerxes::Switch{sim.topology(), config.switch_, "switch0"};
  auto sw1 = xerxes::Switch{sim.topology(), config.switch_, "switch1"};
  auto sw2 = xerxes::Switch{sim.topology(), config.switch_, "switch2"};
  std::vector<xerxes::DRAMsim3Interface *> mems;
  for (int i = 0; i < config.memcnt; ++i) {
    mems.push_back(new xerxes::DRAMsim3Interface{
        sim.topology(), config.clock, config.proce,
        config.capa * (xerxes::Addr)(i), config.config, "output",
        "mem" + std::to_string(i)});
  }

  // Make topology by `add_dev()` and `add_edge()`.
  // Use `build_route()` to acctually build route table.
  // Use `set_entry()` to set the entry point of the simulation.
  sim.system()
      ->add_dev(&host)
      ->add_dev(&bus)
      ->add_dev(&sw0)
      ->add_dev(&sw1)
      ->add_dev(&sw2);
  for (auto i = 0; i < config.memcnt; ++i) {
    sim.system()->add_dev(mems[i]);
  }

  sim.topology()
      ->add_edge(host.id(), bus.id())
      ->add_edge(bus.id(), sw0.id())

      // Chain the switches.
      ->add_edge(sw0.id(), sw1.id())
      ->add_edge(sw0.id(), mems[0]->id())
      ->add_edge(sw1.id(), sw2.id())
      ->add_edge(sw1.id(), mems[1]->id())
      ->add_edge(sw2.id(), mems[2]->id())
      ->add_edge(sw2.id(), mems[3]->id());

  sw0.add_upstream(bus.id());
  sw1.add_upstream(sw0.id());
  sw2.add_upstream(sw1.id());

  /*
        // Tree the switches.
        ->add_edge(sw0.id(), sw1.id())
        ->add_edge(sw0.id(), sw2.id())
        ->add_edge(sw1.id(), mems[0]->id())
        ->add_edge(sw1.id(), mems[1]->id())
        ->add_edge(sw2.id(), mems[2]->id())
        ->add_edge(sw2.id(), mems[3]->id());
  */
  sim.topology()->build_route();
  sim.set_entry(host.id());

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

  auto &notifier = *xerxes::Notifier::glb();
  auto issue_cnt = 0;
  auto ratio = config.ratio;

  while (!host.all_issued()) {
    bool res = true;
    while (res) {
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
    }
    auto rem = notifier.step();
    if (!res || rem == 0) {
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
  // Use `log_route()` to log the route table.
  // sim.topology()->log_route(stats_out);
  host.log_stats(stats_out);
  // bus.log_stats(stats_out);
  sw0.log_stats(stats_out);
  sw1.log_stats(stats_out);
  sw2.log_stats(stats_out);
}
