#pragma once
#ifndef XERXES_STANDALONE_HH
#define XERXES_STANDALONE_HH

#include "def.hh"
#include "simulation.hh"
#include "utils.hh"

namespace xerxes {
struct XerxesConfigs {
  /* -------------------------------- GENERAL ------------------------------- */
  // Endpoint number.
  int ep_num = 1;
  // RW ratio.
  double rw_ratio = 0.1;
  // Max clock.
  Tick max_clock = 1000000;
  // Clock granularity.
  int clock_granu = 10;
  // Log name.
  std::string log_name = "output/try.csv";

  /* ------------------------------- REQUESTER ------------------------------ */
  // Issue delay of the requester.
  Tick issue_delay = 0;
  // Burst size of the requester.
  int burst_size = 1;
  // Queue capacity of the requester.
  int q_capacity = 32;
  // Cache capacity of the requester.
  int cache_capacity = 8192;
  // Cache delay of the requester.
  Tick cache_delay = 12;
  // Coherent of the requester.
  bool coherent = false;
  // Interleaving type of the requester. stream/random/trace
  std::string interleave_type = "stream";
  // Interleaving parameter of the requester.
  size_t interleave_param = 5;
  // Trace file, if needed.
  std::string trace_file = "";

  /* --------------------------------- BUS --------------------------------- */
  // If its full-duplex.
  bool full_duplex = true;
  // Reverse time if half-duplex.
  Tick reverse_time = 0;
  // Bus delay.
  Tick bus_delay = 1;
  // Bus width in bits.
  size_t bus_width = 32;
  // Framing time.
  Tick framing_time = 25;
  // Frame size.
  size_t frame_size = 1;

  /* --------------------------------- DRAM -------------------------------- */
  // Tick per clock.
  Tick tick_per_clock = 1;
  // Controller processing time.
  Tick ctrl_proc_time = 25;
  // Start addr.
  Addr start_addr = 0;
  // Single DRAM capacity.
  Addr dram_capacity = 64 * 1024;
  // DRAM config file.
  std::string dram_config = "DRAMsim3/configs/DDR4_8Gb_x8_3200.ini";
  // Log directory.
  std::string dram_log_dir = "output";
};

void global_init(Simulation *sim, std::ostream &os, XerxesLogLevel level,
                 Packet::XerxesLoggerFunc pkt_logger);

Tick step();
bool events_empty();

XerxesConfigs parse_basic_configs(std::string config_file_name);
} // namespace xerxes

#endif // XERXES_STANDALONE_HH
