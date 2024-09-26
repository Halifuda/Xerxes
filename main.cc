#include "def.hh"
#include "dramsim3_interface.hh"
#include "requester.hh"
#include "xerxes_standalone.hh"

#include <fstream>
#include <iostream>

using namespace std;

int main() {
  auto sim = xerxes::Simulation{};
  auto fout = std::fstream("output/try.csv", std::ios::out);
  fout << "id,type,memid,addr,send,arrive,bus_queuing,bus_time,dram_queuing,"
          "dram_time,total_time"
       << std::endl;
  auto pkt_logger = [&](const xerxes::Packet &pkt) {
    xerxes::XerxesLogger::info()
        << pkt.id << "," << xerxes::TypeName::of(pkt.type) << "," << pkt.src
        << "," << std::hex << pkt.addr << std::dec << "," << pkt.sent << ","
        << pkt.arrive << ","
        << pkt.get_stat(xerxes::NormalStatType::BUS_QUEUE_DELAY) << ","
        << pkt.get_stat(xerxes::NormalStatType::BUS_TIME) << ","
        << pkt.get_stat(xerxes::NormalStatType::DRAM_INTERFACE_QUEUING_DELAY)
        << "," << pkt.get_stat(xerxes::NormalStatType::DRAM_TIME) << ","
        << pkt.arrive - pkt.sent << std::endl;
    return 0;
  };
  xerxes::global_init(&sim, fout, xerxes::XerxesLogLevel::INFO, pkt_logger);
  auto ctx = xerxes::parse_config("configs/test.toml");

  auto &config = ctx.general;
  auto &requesters = ctx.requesters;
  auto &mems = ctx.mems;

  // Run the simulation.
  std::cout << "Start simulation." << std::endl;
  for (auto &requester : requesters)
    requester->register_issue_event(0);
  xerxes::Tick clock_cnt = 0;
  xerxes::Tick last_curt = INT_MAX;
  auto check_all_issued = [&requesters]() {
    for (auto &requester : requesters) {
      if (!requester->all_issued()) {
        return false;
      }
    }
    return true;
  };
  auto check_all_empty = [&requesters]() {
    for (auto &requester : requesters) {
      if (!requester->q_empty()) {
        return false;
      }
    }
    return true;
  };
  std::vector<xerxes::Tick> mems_tick(mems.size(), 0);
  auto clock_all_mems_to_tick = [&mems, &mems_tick, &config](xerxes::Tick tick,
                                                             bool not_changed) {
    for (size_t i = 0; i < mems.size(); ++i)
      mems_tick[i] = 0;
    for (size_t i = 0; i < mems.size(); ++i) {
      for (int g = 0;
           g < config.clock_granu && (mems_tick[i] < tick || not_changed);
           ++g) {
        mems_tick[i] = mems[i]->clock();
      }
    }
  };

  // Simulation.
  while (clock_cnt < config.max_clock) {
    if (check_all_issued()) {
      break;
    }
    auto curt = xerxes::step();
    bool not_changed = last_curt == curt;
    last_curt = curt;
    // TODO: automate clock align.
    clock_all_mems_to_tick(curt, not_changed);
    clock_cnt++;
  }

  while (clock_cnt < config.max_clock) {
    auto curt = xerxes::step();
    bool not_changed = last_curt == curt;
    last_curt = curt;
    clock_all_mems_to_tick(curt, not_changed);
    clock_cnt++;
    if (check_all_empty()) {
      break;
    }
  }
  std::cout << "Simulation finished." << std::endl;
  return 0;
}
