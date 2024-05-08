#include "def.hpp"
#include "xerxes_core.hpp"

#include <fstream>
#include <iomanip>
#include <iostream>

int main() {
  // Make simulation skeleton.
  auto sim = xerxes::Simulation{};
  // Make devices.
  auto requester = xerxes::RequesterBuilder{}
                       .topology(sim.topology())
                       .issue_delay(5)
                       .burst_size(1)
                       .q_capacity(10)
                       .cache_capacity(128)
                       .cache_delay(12)
                       .coherent(false)
                       .interleave(new xerxes::Requester::Stream{120})
                       .build();
  auto bus = xerxes::DuplexBus{sim.topology(), true, 0, 1, 256 * 8, 10, 256};
  auto mem = xerxes::DRAMsim3Interface{sim.topology(),    1,       40, 0,
                                       "output/dram.ini", "output"};

  // Make topology by `add_dev()` and `add_edge()`.
  sim.system()->add_dev(&requester)->add_dev(&bus)->add_dev(&mem);
  sim.topology()
      ->add_edge(requester.id(), bus.id())
      ->add_edge(bus.id(), mem.id());
  // Use `build_route()` to acctually build route table.
  sim.topology()->build_route();
  // Build a default event, triggered when a device is receiving a packet.
  auto default_event = [&](void *topo_id) {
    auto id = (xerxes::TopoID)topo_id;
    auto dev = sim.system()->find_dev(id);
    dev->transit();
  };
  // Build a logger ostream.
  std::fstream fout = std::fstream("output/try.csv", std::ios::out);
  fout << "id,type,addr,send,arrive,total_time" << std::endl;
  // Build a packet logger, triggered when a packet calls `log_stats()`.
  auto pkt_logger = [&](const xerxes::Packet &pkt) {
    xerxes::Logger::info() << pkt.id << "," << xerxes::TypeName::of(pkt.type)
                           << "," << std::hex << pkt.addr << std::dec << ","
                           << pkt.sent << "," << pkt.arrive << ","
                           << pkt.arrive - pkt.sent << std::endl;
    return 0;
  };
  // Initialize the global utils, including notifier, logger stream, log
  // level, pkt logger.
  xerxes::global_init(default_event, fout, xerxes::LogLevel::INFO, pkt_logger);
  // Add end points to the host.
  requester.add_end_point(mem.id(), 0, 64 * 1024, 1);

  // Run the simulation.
  std::cout << "Start simulation." << std::endl;
  auto &engine = *xerxes::EventEngine::glb();
  requester.register_issue_event(0);
  auto clock_cnt = 0;
  while (true && clock_cnt < 1000000 /* Max */) {
    if (requester.all_issued()) {
      break;
    }
    auto curt = engine.step();
    // TODO: automate clock align.
    xerxes::Tick memt = 0;
    for (int i = 0; i < 10 /* clock granularity */ && memt < curt; ++i) {
      memt = mem.clock();
      clock_cnt++;
    }
  }
  while (clock_cnt < 1000000 /* Max */) {
    auto curt = engine.step();
    xerxes::Tick memt = 0;
    for (int i = 0; i < 10 /* clock granularity */ && memt < curt; ++i) {
      memt = mem.clock();
      clock_cnt++;
    }
    if (requester.q_empty()) {
      break;
    }
  }
  return 0;
}
