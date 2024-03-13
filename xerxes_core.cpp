#include "xerxes_core.hpp"

namespace xerxes {
void global_init(NotifierFunc f, std::ostream &os, LogLevel level,
                 Packet::LoggerFunc pkt_logger) {
  Notifier::glb(new Notifier(f));
  Logger::set(os, level);
  Packet::pkt_logger(true, pkt_logger);
}
} // namespace xerxes
