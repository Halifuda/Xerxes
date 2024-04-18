#include "def.hpp"
#include "xerxes_core.hpp"

#include <fstream>
#include <iomanip>
#include <iostream>

int main() {
  // Make simulation skeleton.
  auto sim = xerxes::Simulation{};
  // Make devices.
  auto requester = xerxes::Requester{
      sim.topology(), 20, 128, 12, 600, 10, 1, new xerxes::Requester::Stream{}};
  auto bus = xerxes::DuplexBus{sim.topology(), true, 0, 1, 256 * 8, 10, 256};
  auto mem = xerxes::DRAMsim3Interface{
      sim.topology(), 1, 40, 0, "Input/DRAMsim3/config/file/here", "output"};

  // Make topology by `add_dev()` and `add_edge()`.
  sim.system()->add_dev(&requester)->add_dev(&bus)->add_dev(&mem);
  sim.topology()
      ->add_edge(requester.id(), bus.id())
      ->add_edge(bus.id(), mem.id());
  // Use `build_route()` to acctually build route table.
  sim.topology()->build_route();
  // Build a notifier, triggered when a device is receiving a packet.
  auto notifier_func = [&](void *topo_id) {
    auto id = (xerxes::TopoID)topo_id;
    auto dev = sim.system()->find_dev(id);
    dev->transit();
  };
  // Build a logger ostream.
  std::fstream fout = std::fstream("Input/log/directory/here", std::ios::out);
  fout << "id,type,addr,total_time" << std::endl;
  // Build a packet logger, triggered when a packet calls `log_stats()`.
  auto pkt_logger = [&](const xerxes::Packet &pkt) {
    xerxes::Logger::info() << pkt.id << "," << xerxes::TypeName::of(pkt.type)
                           << "," << std::hex << pkt.addr << std::dec << ","
                           << pkt.arrive - pkt.sent << std::endl;
    return 0;
  };
  // Initialize the global utils, including notifier, logger stream, log
  // level, pkt logger.
  xerxes::global_init(notifier_func, fout, xerxes::LogLevel::INFO, pkt_logger);
  // Add end points to the host.
  requester.add_end_point(mem.id(), 0, 64 * 1024, 1);

  // Run the simulation.
  auto &notifier = *xerxes::Notifier::glb();
  auto clock_cnt = 0;
  while (true && clock_cnt < 100000 /* Max */) {
    if (requester.all_issued()) {
      break;
    }
    bool res = true;
    while (res) {
      res = requester.step(false);
    }
    notifier.step();
    for (int i = 0; i < 10 /* clock granularity */; ++i) {
      mem.clock();
      clock_cnt++;
    }
  }
  while (clock_cnt < 100000 /* Max */) {
    notifier.step();
    for (int i = 0; i < 10 /* clock granularity */; ++i) {
      mem.clock();
      clock_cnt++;
    }
    if (requester.q_empty()) {
      break;
    }
  }
  return 0;
}
