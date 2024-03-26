#include "def.hpp"
#include "xerxes_core.hpp"

#include <fstream>
#include <iostream>

// Issuing def.
#define CNT (1000 * MEMCNT + HOSTQ)
#define HOSTQ (16 * MEMCNT)
#define HOSTD 0
#define PACKAGE_NUM 1
#define BURST 3
#define RANDOM false
// Bus def.
#define IS_FULL true
#define HALFT 15
#define TPERT 1
#define BYTE_WIDTH 32
#define BWIDTH (8 * BYTE_WIDTH)
#define FRAMING 40
// Switch def.
#define SWITCH 0 // 0 means oracle
// DRAM def.
#define CAPA (64 * 500)
#define CLOCK 1
#define PROCE 40
#define GRAIN 10
#define MAX_CLOCK 10000
// Snoop def.
#define SNOOP 20
#define LINEN 128
#define ASSOC 16

#define LOGLEVEL xerxes::LogLevel::INFO
#define RATIO 0
#define MEMCNT 16
char OUTPUT[] = "output/bus_mem3.csv";
char STATS_OUT[] = "output/bus_mem3.txt";
char CONFIG[] = "output/dram.ini";

int main() {
  // Make simulation skeleton.
  auto sim = xerxes::Simulation{};

  // Make devices.
  auto host = xerxes::Host{sim.topology(), HOSTQ, SNOOP, CNT, HOSTD, BURST};
  auto snoop = xerxes::Snoop{
      sim.topology(), LINEN, ASSOC, new xerxes::Snoop::FIFO{}, false, "snoop0"};
  auto bus =
      xerxes::DuplexBus{sim.topology(), IS_FULL, HALFT, TPERT, BWIDTH, FRAMING};

  auto pkg0 = xerxes::Packaging{sim.topology(), PACKAGE_NUM, "package0"};
  auto pkg1 = xerxes::Packaging{sim.topology(), PACKAGE_NUM, "package1"};

  auto sw = xerxes::Switch{sim.topology(), SWITCH, "switch"};
  std::vector<xerxes::DRAMsim3Interface *> mems;
  std::vector<xerxes::BurstHandler *> bursts;
  for (int i = 0; i < MEMCNT; ++i) {
    bursts.push_back(
        new xerxes::BurstHandler{sim.topology(), "burst" + std::to_string(i)});
    mems.push_back(new xerxes::DRAMsim3Interface{
        sim.topology(), CLOCK, PROCE, CAPA * (xerxes::Addr)(i), CONFIG,
        "output", "mem" + std::to_string(i)});
  }

  // Make topology by `add_dev()` and `add_edge()`.
  // Use `build_route()` to acctually build route table.
  // Use `set_entry()` to set the entry point of the simulation.
  sim.system()->add_dev(&host)->add_dev(&bus)->add_dev(&sw)->add_dev(&snoop);
  for (auto i = 0; i < MEMCNT; ++i) {
    sim.system()->add_dev(bursts[i]);
    sim.system()->add_dev(mems[i]);
  }
  sim.system()->add_dev(&pkg0)->add_dev(&pkg1);

  sim.topology()
      ->add_edge(host.id(), pkg0.id())
      ->add_edge(pkg0.id(), bus.id())
      ->add_edge(bus.id(), pkg1.id())
      ->add_edge(pkg1.id(), sw.id());
  for (auto i = 0; i < MEMCNT; ++i) {
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
  std::fstream fout = std::fstream(OUTPUT, std::ios::out);
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
  xerxes::global_init(notifier_func, fout, LOGLEVEL, pkt_logger);

  // Add end points to the host.
  for (size_t i = 0; i < mems.size(); ++i) {
    host.add_end_point(mems[i]->id(), CAPA * i, CAPA * (i + 1), RANDOM);
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
  auto ratio = RATIO;

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
      for (int i = 0; i < GRAIN; ++i) {
        for (auto mem : mems) {
          mem->clock();
        }
      }
    }
  }
  auto clock_cnt = 0;
  while (!host.q_empty() && (clock_cnt++) < MAX_CLOCK) {
    notifier.step();
    for (int i = 0; i < GRAIN; ++i) {
      for (auto mem : mems) {
        mem->clock();
      }
    }
  }

  // Log the statistics of the devices.
  std::fstream stats_out = std::fstream(STATS_OUT, std::ios::out);
  host.log_stats(stats_out);
  bus.log_stats(stats_out);

  return 0;
}
