#include "xerxes_standalone.hh"
#include "device.hh"
#include "utils.hh"

namespace xerxes {
Simulation *glb_sim = nullptr;

void global_init(Simulation *sim, std::ostream &os, LogLevel level,
                 Packet::LoggerFunc pkt_logger) {
  glb_sim = sim;
  Logger::set(os, level);
  Packet::pkt_logger(true, pkt_logger);
}

class EventEngine {
  std::multimap<Tick, EventFunc> events;

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

  bool empty() { return events.empty(); }
} glb_engine;

void Device::sched_transit(Tick tick) {
  glb_engine.add(tick, [this]() { this->transit(); });
}

void xerxes_schedule(EventFunc f, uint64_t tick) { glb_engine.add(tick, f); }

bool xerxes_events_empty() { return glb_engine.empty(); }

Tick step() { return glb_engine.step(); }

bool events_empty() { return glb_engine.empty(); }
} // namespace xerxes
