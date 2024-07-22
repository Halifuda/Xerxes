#include "bus.hh"
#include "def.hh"
#include "dramsim3_interface.hh"
#include "requester.hh"
#include "xerxes_standalone.hh"

#include <fstream>
#include <iostream>

xerxes::Requester build_requester(xerxes::Simulation *sim,
                                  const xerxes::XerxesConfigs &config) {
  auto builder = xerxes::RequesterBuilder{}
                     .simulation(sim)
                     .issue_delay(config.issue_delay)
                     .burst_size(config.burst_size)
                     .q_capacity(config.q_capacity)
                     .cache_capacity(config.cache_capacity)
                     .cache_delay(config.cache_delay)
                     .coherent(config.coherent);
  if (config.interleave_type == "stream") {
    builder.interleave(new xerxes::Requester::Stream{config.interleave_param});
  } else if (config.interleave_type == "random") {
    builder.interleave(new xerxes::Requester::Random{config.interleave_param});
  } else if (config.interleave_type == "trace") {
    builder.interleave(new xerxes::Requester::Trace{config.trace_file});
  } else {
    std::cerr << "Unknown interleave type: " << config.interleave_type
              << std::endl;
    exit(1);
  }
  return builder.build();
}

int main(int argc, char **argv) {
  // Make simulation skeleton.
  auto sim = xerxes::Simulation{};
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <config_file>" << std::endl;
    return 1;
  }
  // Parse the config file.
  auto config = xerxes::parse_basic_configs(argv[1]);
  // Make devices.
  std::vector<xerxes::Requester> requesters;
  std::vector<xerxes::DRAMsim3Interface *> mems;

  auto bus = xerxes::DuplexBus{&sim,
                               config.full_duplex,
                               config.reverse_time,
                               config.bus_delay,
                               config.bus_width,
                               config.framing_time,
                               config.frame_size};
  for (int i = 0; i < config.ep_num; ++i) {
    requesters.push_back(build_requester(&sim, config));
    mems.push_back(new xerxes::DRAMsim3Interface{
        &sim, config.tick_per_clock, config.ctrl_proc_time, config.start_addr,
        config.dram_config, config.dram_log_dir});
  }

  // Make topology by `add_dev()` and `add_edge()`.
  sim.system()->add_dev(&bus);
  for (int i = 0; i < config.ep_num; ++i) {
    sim.system()->add_dev(&requesters[i])->add_dev(mems[i]);
    sim.topology()
        ->add_edge(requesters[i].id(), bus.id())
        ->add_edge(bus.id(), mems[i]->id());
  }
  // Use `build_route()` to acctually build route table.
  sim.topology()->build_route();

  // Build a logger ostream.
  std::fstream fout = std::fstream(config.log_name, std::ios::out);
  fout << "id,type,memid,addr,send,arrive,bus_queuing,bus_time,dram_queuing,"
          "dram_time,total_time"
       << std::endl;
  // Build a packet logger, triggered when a packet calls `log_stats()`.
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
  // Initialize the global utils, including notifier, logger stream, log
  // level, pkt logger.
  xerxes::global_init(&sim, fout, xerxes::XerxesLogLevel::INFO, pkt_logger);
  // Add end points to the host.
  for (int i = 0; i < config.ep_num; ++i) {
    for (int j = 0; j < config.ep_num; ++j) {
      requesters[i].add_end_point(mems[j]->id(), 0 + j * config.dram_capacity,
                                  (j + 1) * config.dram_capacity,
                                  config.rw_ratio);
    }
  }

  // Run the simulation.
  std::cout << "Start simulation." << std::endl;
  for (auto &requester : requesters)
    requester.register_issue_event(0);
  xerxes::Tick clock_cnt = 0;
  xerxes::Tick last_curt = INT_MAX;
  auto check_all_issued = [&requesters]() {
    for (auto &requester : requesters) {
      if (!requester.all_issued()) {
        return false;
      }
    }
    return true;
  };
  auto check_all_empty = [&requesters]() {
    for (auto &requester : requesters) {
      if (!requester.q_empty()) {
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
