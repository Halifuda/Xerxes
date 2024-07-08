#pragma once
#ifndef XERXES_STANDALONE_HH
#define XERXES_STANDALONE_HH

#include "def.hh"
#include "simulation.hh"
#include "utils.hh"

namespace xerxes {
void global_init(Simulation *sim, std::ostream &os, LogLevel level,
                 Packet::LoggerFunc pkt_logger);

Tick step();
bool events_empty();
} // namespace xerxes

#endif // XERXES_STANDALONE_HH
