#ifndef XERXES_STANDALONE_CC
#define XERXES_STANDALONE_CC

#include "xerxes_standalone.hh"
#include "simplessd/sim/simulator.hh"
#include "device.hh"
#include "utils.hh"

#include "ext/toml.hpp"

namespace xerxes {
Simulation *glb_sim = nullptr;

class EventEngine: public SimpleSSD::Simulator {
  std::multimap<Tick, EventFunc> events;
  SimpleSSD::Event counter;
  std::unordered_map<SimpleSSD::Event, SimpleSSD::EventFunction> eventTable;

public:
  EventEngine() {}

  static EventEngine *glb(EventEngine *n = nullptr) {
    static EventEngine *engine = nullptr;
    if (n != nullptr)
      engine = n;
    return engine;
  }

  void add(Tick tick, EventFunc f) { events.insert(std::make_pair(tick, f)); }

  Tick step() {
    if (!events.empty()) {
      auto tick = events.begin()->first;
      auto event = events.begin()->second;
      events.erase(events.begin());
      event();
      return tick;
    }
    return 0;
  }

  // for simplessd
  uint64_t getCurrentTick() override {
    // TODO: 貌似Xerxes没有维护Tick？
    return 0;
  }

  SimpleSSD::Event allocateEvent(SimpleSSD::EventFunction func) override {
    auto iter = eventTable.insert({++counter, func});

    if (!iter.second) assert(0);

    return counter;
  }

  void scheduleEvent(SimpleSSD::Event eid, uint64_t tick) override {
    auto iter = eventTable.find(eid);
  
    if (iter != eventTable.end()) {
      // TODO: Convert SimpleSSD EventFunction to Xerxes EventFunc
      SimpleSSD::EventFunction f = iter->second;
      add(tick, [f,tick](){f(tick);});
    }
    else assert(0);
  }

  void descheduleEvent(SimpleSSD::Event eid) override {
    // TODO
    assert(0);
  }

  bool isScheduled(SimpleSSD::Event eid, uint64_t *pTick) override {
    bool ret = false;
    auto iter = eventTable.find(eid);

    if (iter != eventTable.end()) {
      // TODO
      assert(0);
    }
    else assert(0);

    return ret;
  }

  void deallocateEvent(SimpleSSD::Event eid) override {
    auto iter = eventTable.find(eid);

    if (iter != eventTable.end()) {
      // TODO: remove event from this.events
      eventTable.erase(iter);
    }
    else assert(0);
  }

  bool empty() { return events.empty(); }
} glb_engine;

void global_init(Simulation *sim, std::ostream &os, XerxesLogLevel level,
                 Packet::XerxesLoggerFunc pkt_logger) {
  glb_sim = sim;
  SimpleSSD::setSimulator(&glb_engine);
  XerxesLogger::set(os, level);
  Packet::pkt_logger(true, pkt_logger);
}

void Device::sched_transit(Tick tick) {
  glb_engine.add(tick, [this]() { this->transit(); });
}

void xerxes_schedule(EventFunc f, uint64_t tick) { glb_engine.add(tick, f); }

bool xerxes_events_empty() { return glb_engine.empty(); }

Tick step() { return glb_engine.step(); }

bool events_empty() { return glb_engine.empty(); }

XerxesConfigs parse_basic_configs(std::string config_file_name) {
  toml::value config;
  try {
    config = toml::parse(config_file_name);
  } catch (const std::exception &e) {
    PANIC(std::string{"Error: "} + e.what());
  }
  auto basic = XerxesConfigs{};

  // General.
  basic.ep_num = toml::find_or<int>(config, "ep_num", basic.ep_num);
  basic.rw_ratio = toml::find_or<int>(config, "rw_ratio", basic.rw_ratio);
  basic.max_clock = toml::find_or<int>(config, "max_clock", basic.max_clock);
  basic.clock_granu =
      toml::find_or<int>(config, "clock_granu", basic.clock_granu);
  basic.log_name =
      toml::find_or<std::string>(config, "log_name", basic.log_name);
  // Requester.
  basic.issue_delay =
      toml::find_or<int>(config, "requester", "issue_delay", basic.issue_delay);
  basic.burst_size =
      toml::find_or<int>(config, "requester", "burst_size", basic.burst_size);
  basic.q_capacity =
      toml::find_or<int>(config, "requester", "q_capacity", basic.q_capacity);
  basic.cache_capacity = toml::find_or<int>(
      config, "requester", "cache_capacity", basic.cache_capacity);
  basic.cache_delay =
      toml::find_or<int>(config, "requester", "cache_delay", basic.cache_delay);
  basic.coherent =
      toml::find_or<bool>(config, "requester", "coherent", basic.coherent);
  basic.interleave_type = toml::find_or<std::string>(
      config, "requester", "interleave_type", basic.interleave_type);
  basic.interleave_param = toml::find_or<int>(
      config, "requester", "interleave_param", basic.interleave_param);
  basic.trace_file = toml::find_or<std::string>(config, "requester",
                                                "trace_file", basic.trace_file);

  // Bus.
  basic.full_duplex =
      toml::find_or<bool>(config, "bus", "full_duplex", basic.full_duplex);
  basic.reverse_time =
      toml::find_or<int>(config, "bus", "reverse_time", basic.reverse_time);
  basic.bus_delay =
      toml::find_or<int>(config, "bus", "bus_delay", basic.bus_delay);
  basic.bus_width =
      toml::find_or<int>(config, "bus", "bus_width", basic.bus_width);
  basic.framing_time =
      toml::find_or<int>(config, "bus", "framing_time", basic.framing_time);
  basic.frame_size =
      toml::find_or<int>(config, "bus", "frame_size", basic.frame_size);

  // DRAM.
  basic.tick_per_clock = toml::find_or<int>(config, "dram", "tick_per_clock",
                                            basic.tick_per_clock);
  basic.ctrl_proc_time = toml::find_or<int>(config, "dram", "ctrl_proc_time",
                                            basic.ctrl_proc_time);
  basic.start_addr =
      toml::find_or<int>(config, "dram", "start_addr", basic.start_addr);
  basic.dram_capacity =
      toml::find_or<int>(config, "dram", "dram_capacity", basic.dram_capacity);
  basic.dram_config = toml::find_or<std::string>(config, "dram", "dram_config",
                                                 basic.dram_config);
  basic.dram_log_dir =
      toml::find_or<std::string>(config, "dram", "log_dir", basic.dram_log_dir);

  return basic;
};

} // namespace xerxes

#endif // XERXES_STANDALONE_CC
