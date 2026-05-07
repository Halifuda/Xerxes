#pragma once
#ifndef XERXES_STANDALONE_HH
#define XERXES_STANDALONE_HH

#include "def.hh"
#include "device.hh"
#include "dramsim3_interface.hh"
#include "requester.hh"
#include "simulation.hh"
#include "utils.hh"

namespace xerxes {

class Switch;
class Snoop;

// Edge cost entry for TOML config: [["from", "to", cost], ...].
struct EdgeCostEntry {
    std::string from;
    std::string to;
    double cost = 1.0;
};

// General configurations for a Xerxes simulation.
struct XerxesConfig {
    // Max clocking times.
    Tick max_clock = 1000000;
    // Clock granularity (for DRAMsim3).
    int clock_granu = 10;
    // Log level.
    std::string log_level = "INFO";
    // Log file name.
    std::string log_name = "output/try.csv";
    // Device list, <name, type>.
    std::map<std::string, std::string> devices;
    // Edge, <from, to>.
    std::vector<std::pair<std::string, std::string>> edges;
};

class Requester;
class DRAMsim3Interface;
class AddressSystem;

// Structured data from a TOML configuration file.
struct XerxesContext {
    // General configurations.
    XerxesConfig general;
    // Mapping device names to their IDs.
    std::map<std::string, TopoID> name_to_id;
    // All requesters.
    std::vector<Requester *> requesters;
    // All DRAMsim3 endpoints.
    std::vector<DRAMsim3Interface *> mems;
    // All switches (for PBR).
    std::vector<std::pair<TopoID, Switch*>> switches;
    // All snoops.
    std::vector<Snoop *> snoops;
    // All endpoint IDs (requesters + mems + snoops).
    std::vector<TopoID> endpoint_ids;
    // PBR routing policy.
    Topology::RoutingPolicy routing_policy = Topology::BFS;
};

// Used for logging packet information, if the logger is not set by the user.
void default_logger(const Packet &pkt);
// Set the global simulation object.
void init_sim(Simulation *sim);
// Set the packet logger.
void set_pkt_logger(std::ostream &os, XerxesLogLevel level,
                    Packet::XerxesLoggerFunc pkt_logger = default_logger);

// Step the simulation to the next event.
Tick step();
// Check if there are any events in the simulation queue.
bool events_empty();

// Parse the configuration file and return a XerxesContext object.
XerxesContext parse_config(std::string config_file_name);

// Log statistics of all devices.
void log_stats(std::ostream &os);
// Log summary metrics only (CSV format).
void log_summary(std::ostream &os);
} // namespace xerxes

TOML11_DEFINE_CONVERSION_NON_INTRUSIVE(xerxes::EdgeCostEntry, from, to, cost);

TOML11_DEFINE_CONVERSION_NON_INTRUSIVE(xerxes::XerxesConfig, max_clock,
                                       clock_granu, log_level, log_name,
                                       devices, edges);

#endif // XERXES_STANDALONE_HH
