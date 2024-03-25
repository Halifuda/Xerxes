#include "def.hpp"
#include "xerxes_core.hpp"

#include <fstream>
#include <iostream>

// Issuing def.
#define CNT (2000 + HOSTQ)
#define HOSTQ 80
#define HOSTD 0
// Bus def.
#define IS_FULL true
#define HALFT 15
#define TPERT 1
#define BWIDTH 64
#define FRAMING 40
#define SWITCH 40
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
#define RATIO 1
char OUTPUT[] = "output/snoop.csv";
char STATS_OUT[] = "output/snoop.txt";
char CONFIG[] = "output/dram.ini";

int main() {
  // Make simulation skeleton.
  auto sim = xerxes::Simulation{};

  // Make devices.
  auto host = xerxes::Host{sim.topology(), HOSTQ, SNOOP, CNT, HOSTD};
  auto snoop = xerxes::Snoop{
      sim.topology(), LINEN, ASSOC, new xerxes::Snoop::FIFO{}, false, "snoop0"};
  auto bus =
      xerxes::DuplexBus{sim.topology(), IS_FULL, HALFT, TPERT, BWIDTH, FRAMING};
  auto sw = xerxes::Switch{sim.topology(), SWITCH, "switch"};
  auto mem0 = xerxes::DRAMsim3Interface{sim.topology(), CLOCK,    PROCE, 0,
                                        CONFIG,         "output", "mem0"};
  auto mem1 = xerxes::DRAMsim3Interface{sim.topology(), CLOCK,    PROCE, CAPA,
                                        CONFIG,         "output", "mem1"};
  auto mem2 = xerxes::DRAMsim3Interface{
      sim.topology(), CLOCK, PROCE, CAPA * 2, CONFIG, "output", "mem2"};
  auto mem3 = xerxes::DRAMsim3Interface{
      sim.topology(), CLOCK, PROCE, CAPA * 3, CONFIG, "output", "mem3"};

  // Make topology by `add_dev()` and `add_edge()`.
  // Use `build_route()` to acctually build route table.
  // Use `set_entry()` to set the entry point of the simulation.
  sim.system()
      ->add_dev(&host)
      ->add_dev(&bus)
      ->add_dev(&sw)
      ->add_dev(&snoop)
      ->add_dev(&mem0)
      ->add_dev(&mem1)
      ->add_dev(&mem2)
      ->add_dev(&mem3);
  sim.topology()
      ->add_edge(host.id(), bus.id())
      ->add_edge(bus.id(), snoop.id())
      ->add_edge(snoop.id(), mem0.id());
  /*
  ->add_edge(bus.id(), sw.id())
  ->add_edge(sw.id(), mem0.id())
  ->add_edge(sw.id(), mem1.id())
  ->add_edge(sw.id(), mem2.id())
  ->add_edge(sw.id(), mem3.id());*/
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
  fout << "id,type,addr,sent,arrive,device_process_time,dram_q_time,dram_time,"
          "framing_time,bus_q_time,bus_time,switch_q_time,switch_time,snoop_"
          "evict_time,host_inv_time,total_time"
       << std::endl;
  auto pkt_logger = [&](const xerxes::Packet &pkt) {
    auto log_stat = [&](xerxes::NormalStatType stat) {
      if (pkt.has_stat(stat) > 0) {
        xerxes::Logger::info() << "," << pkt.get_stat(stat);
      } else {
        xerxes::Logger::info() << ",0";
      }
    };

    xerxes::Logger::info() << pkt.id << "," << xerxes::TypeName::of(pkt.type);
    xerxes::Logger::info() << "," << pkt.addr << "," << pkt.sent << ","
                           << pkt.arrive;
    log_stat(xerxes::DEVICE_PROCESS_TIME);
    log_stat(xerxes::DRAM_INTERFACE_QUEUING_DELAY);
    log_stat(xerxes::DRAM_TIME);
    log_stat(xerxes::FRAMING_TIME);
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
  host.add_end_point(mem0.id(), 0, CAPA, true);
  // host.add_end_point(mem1.id(), CAPA, CAPA);
  // host.add_end_point(mem2.id(), CAPA * 2, CAPA);
  // host.add_end_point(mem3.id(), CAPA * 3, CAPA);

  // Add topology to the switch.
  // sw.add_upstream(host.id());
  // sw.add_downstream(mem0.id());
  // sw.add_downstream(mem1.id());
  // sw.add_downstream(mem2.id());
  // sw.add_downstream(mem3.id());

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
    xerxes::Logger::debug() << "Host step " << res << std::endl;
    auto rem = notifier.step();
    xerxes::Logger::debug() << "Notifier remind " << rem << std::endl;
    if (!res) {
      mem0.clock_until();
      mem1.clock_until();
      mem2.clock_until();
      mem3.clock_until();
    }
  }
  auto clock_cnt = 0;
  while (!host.q_empty() && (clock_cnt++) < MAX_CLOCK) {
    notifier.step();
    mem0.clock_until();
    mem1.clock_until();
    mem2.clock_until();
    mem3.clock_until();
    for (int i = 0; i < GRAIN; ++i) {
      mem0.clock();
      mem1.clock();
      mem2.clock();
      mem3.clock();
    }
  }

  // Log the statistics of the devices.
  std::fstream stats_out = std::fstream(STATS_OUT, std::ios::out);
  host.log_stats(stats_out);
  bus.log_stats(stats_out);

  return 0;
}
