#include "xerxes_core.hpp"

namespace xerxes {
void global_init(EventFunc default_event, std::ostream &os, LogLevel level,
                 Packet::LoggerFunc pkt_logger) {
  EventEngine::glb(new EventEngine(default_event));
  Logger::set(os, level);
  Packet::pkt_logger(true, pkt_logger);
}
} // namespace xerxes
