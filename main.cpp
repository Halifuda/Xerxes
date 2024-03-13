#include "def.hpp"
#include "xerxes_core.hpp"

#include <fstream>
#include <iostream>

// Issuing def.
#define CNT 180
#define HOSTQ 8
#define HOSTD 0
#define PKTTY xerxes::PacketType::NT_RD
// Bus def.
#define BWIDTH 64
#define FRAMING 20
// DRAM def.
#define CAPA (64 * 500)
#define GRAIN 10
#define MAX_CLOCK 10000
// Snoop def.
#define SNOOP 20
#define LINEN 7
#define ASSOC 1
char OUTPUT[] = "output/try.csv";

int main() {
  // Make simulation skeleton.
  auto sim = xerxes::Simulation{};

  // Make devices.
  auto host = xerxes::Host{sim.topology(), HOSTQ, SNOOP, CNT, HOSTD};
  // auto snoop =
  // xerxes::Snoop{sim.topology(), LINEN, ASSOC, new xerxes::Snoop::FIFO{}};
  auto bus = xerxes::DuplexBus{sim.topology(), false, 1, BWIDTH, FRAMING};
  auto mem0 = xerxes::DRAMsim3Interface{
      sim.topology(), 1,      0, "DRAMsim3/configs/DDR4_8Gb_x8_3200.ini",
      "output",       "DRAM0"};
  auto mem1 = xerxes::DRAMsim3Interface{
      sim.topology(), 1,      CAPA, "DRAMsim3/configs/DDR4_8Gb_x8_3200.ini",
      "output",       "DRAM1"};

  // Make topology by `add_dev()` and `add_edge()`.
  // Use `build_route()` to acctually build route table.
  // Use `set_entry()` to set the entry point of the simulation.
  sim.system()->add_dev(&host)->add_dev(&bus)->add_dev(&mem0)->add_dev(&mem1);
  sim.topology()
      ->add_edge(host.id(), bus.id())
      ->add_edge(bus.id(), mem0.id())
      ->add_edge(bus.id(), mem1.id())
      ->build_route();
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
  fout << "id,type,addr,sent,arrive,dram_q_time,dram_time,framing_time,bus_"
          "time,total_time"
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
    log_stat(xerxes::DRAM_INTERFACE_QUEUING_DELAY);
    log_stat(xerxes::DRAM_TIME);
    log_stat(xerxes::FRAMING_TIME);
    log_stat(xerxes::BUS_TIME);
    xerxes::Logger::info() << "," << pkt.arrive - pkt.sent << std::endl;
  };

  // Initialize the global utils, including notifier, logger stream, log level,
  // pkt logger.
  xerxes::global_init(notifier_func, fout, xerxes::LogLevel::INFO, pkt_logger);

  // Add end points to the host.
  host.add_end_point(mem0.id(), 0, CAPA);
  host.add_end_point(mem1.id(), CAPA, CAPA);

  auto &notifier = *xerxes::Notifier::glb();
  while (!host.all_issued()) {
    auto res = host.step(PKTTY);
    xerxes::Logger::debug() << "Host step " << res << std::endl;
    notifier.step();
    if (!res) {
      mem0.clock_until();
      mem1.clock_until();
    }
  }
  auto clock_cnt = 0;
  while (!host.q_empty() && (clock_cnt++) < MAX_CLOCK) {
    notifier.step();
    mem0.clock_until();
    mem1.clock_until();
    for (int i = 0; i < GRAIN; ++i) {
      mem0.clock();
      mem1.clock();
    }
  }

  return 0;
}
