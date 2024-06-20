#pragma once
#include "def.hpp"
#include "simulation.hpp"
#include "utils.hpp"

namespace xerxes {
void global_init(Simulation *sim, std::ostream &os, LogLevel level,
                 Packet::LoggerFunc pkt_logger);

Tick step();
bool events_empty();
} // namespace xerxes
