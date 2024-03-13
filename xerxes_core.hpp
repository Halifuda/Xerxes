#pragma once
#include "def.hpp"
#include "simulation.hpp"
#include "utils.hpp"

namespace xerxes {
void global_init(NotifierFunc f, std::ostream &os, LogLevel level,
                 Packet::LoggerFunc pkt_logger);
}
