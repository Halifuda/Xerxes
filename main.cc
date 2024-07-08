#include "bus.hh"
#include "def.hh"
#include "dramsim3_interface.hh"
#include "requester.hh"
#include "xerxes_standalone.hh"

#include <fstream>
#include <iostream>

int main() {
  // Make simulation skeleton.
  auto sim = xerxes::Simulation{};
  // Make devices.
  auto requester = xerxes::RequesterBuilder{}
                       .simulation(&sim)
                       .issue_delay(0)
                       .burst_size(1)
                       .q_capacity(32)
                       .cache_capacity(128)
                       .cache_delay(12)
                       .coherent(false)
                       .interleave(new xerxes::Requester::Stream{500})
                       .build();
  auto bus = xerxes::DuplexBus{&sim, true, 0, 1, 64 * 8, 10, 1};
  auto mem =
      xerxes::DRAMsim3Interface{&sim, 1, 40, 0, "output/dram.ini", "output"};

  // Make topology by `add_dev()` and `add_edge()`.
  sim.system()->add_dev(&requester)->add_dev(&bus)->add_dev(&mem);
  sim.topology()
      ->add_edge(requester.id(), bus.id())
      ->add_edge(bus.id(), mem.id());
  // Use `build_route()` to acctually build route table.
  sim.topology()->build_route();

  double ratio = 0.1;
  auto logname = "output/try" + std::to_string(ratio) + ".csv";
  // Build a logger ostream.
  std::fstream fout = std::fstream(logname, std::ios::out);
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
  xerxes::global_init(&sim, fout, xerxes::LogLevel::INFO, pkt_logger);
  // Add end points to the host.
  requester.add_end_point(mem.id(), 0, 64 * 1024, ratio);

  // Run the simulation.
  std::cout << "Start simulation." << std::endl;
  requester.register_issue_event(0);
  auto clock_cnt = 0;
  xerxes::Tick last_curt = INT_MAX;
  while (clock_cnt < 1000000 /* Max */) {
    if (requester.all_issued()) {
      break;
    }
    auto curt = xerxes::step();
    bool not_changed = last_curt == curt;
    last_curt = curt;
    // TODO: automate clock align.
    xerxes::Tick memt = 0;
    for (int i = 0;
         i < 10 /* clock granularity */ && (memt < curt || not_changed); ++i) {
      memt = mem.clock();
      clock_cnt++;
    }
  }
  while (clock_cnt < 1000000 /* Max */) {
    auto curt = xerxes::step();
    bool not_changed = last_curt == curt;
    last_curt = curt;
    xerxes::Tick memt = 0;
    for (int i = 0;
         i < 10 /* clock granularity */ && (memt < curt || not_changed); ++i) {
      memt = mem.clock();
      clock_cnt++;
    }
    if (requester.q_empty()) {
      break;
    }
  }
  return 0;
}
