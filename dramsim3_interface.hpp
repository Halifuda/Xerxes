#pragma once
#include "DRAMsim3/src/memory_system.h"
#include "def.hpp"
#include "device.hpp"
#include "utils.hpp"

#include <list>
#include <map>

namespace xerxes {

class DRAMsim3Interface : public Device {
private:
  Addr start;

  std::vector<Packet> pending;
  std::map<Addr, std::list<Packet>> issued;

  Tick tick_per_clock;
  Tick interface_clock = 0;

  dramsim3::MemorySystem memsys;

  void issue() {
    std::vector<std::vector<Packet>::iterator> to_erase;
    for (auto it = pending.begin(); it != pending.end(); ++it) {
      auto &pkt = *it;
      // Tick to the packet arrival
      while (interface_clock < pkt.arrive) {
        memsys.ClockTick();
        ++interface_clock;
      }
      // TODO: issue delay
      if (memsys.WillAcceptTransaction(pkt.addr - start, pkt.is_write())) {
        if (issued.find(pkt.addr) == issued.end())
          issued[pkt.addr] = std::list<Packet>();
        issued[pkt.addr].push_back(pkt);
        to_erase.push_back(it);
        pkt.delta_stat(DRAM_INTERFACE_QUEUING_DELAY,
                       (double)(interface_clock * tick_per_clock - pkt.arrive));
        pkt.arrive = interface_clock * tick_per_clock;
        memsys.AddTransaction(pkt.addr - start, pkt.is_write());
      }
    }
    for (auto it : to_erase) {
      pending.erase(it);
    }
  }

public:
  DRAMsim3Interface(Topology *topology, const Tick tick_per_clock,
                    const Addr start, const std::string &config_file,
                    const std::string &output_dir,
                    std::string name = "DRAMsim3Interface")
      : Device(topology, name), start(start), tick_per_clock(tick_per_clock),
        memsys(config_file, output_dir,
               std::bind(&DRAMsim3Interface::callback, this,
                         std::placeholders::_1),
               std::bind(&DRAMsim3Interface::callback, this,
                         std::placeholders::_1)) {}

  Addr start_addr() const { return start; }

  void transit() override {
    auto pkt = receive_pkt();
    while (pkt.type != PacketType::PKT_TYPE_NUM) {
      if (pkt.dst == self) {
        Logger::debug() << name_ << " receive packet " << pkt.id << " from "
                        << pkt.from << " at " << pkt.arrive << std::endl;
        pending.push_back(pkt);
      } else {
        send_pkt(pkt);
      }
      pkt = receive_pkt();
    }
    return issue();
  }

  void callback(Addr addr) {
    Logger::debug() << "Callback 0x" << std::hex << addr << std::dec << " at "
                    << std::dec << interface_clock * tick_per_clock
                    << std::endl;
    auto it = issued.find(addr + start);
    if (it == issued.end())
      return;
    auto &pkt = it->second.front();
    std::swap(pkt.src, pkt.dst);

    // TODO: is the callback called at the exact tick?
    pkt.delta_stat(DRAM_TIME,
                   (double)(interface_clock * tick_per_clock - pkt.arrive));
    pkt.arrive = interface_clock * tick_per_clock;
    pkt.is_rsp = true;
    send_pkt(pkt);

    it->second.pop_front();
    if (it->second.empty())
      issued.erase(it); // Save memory
  }

  void clock() {
    memsys.ClockTick();
    ++interface_clock;
  }

  bool clock_until() {
    auto num = issued.size();
    while (num != 0 && issued.size() == num) {
      memsys.ClockTick();
      ++interface_clock;
    }
    if (num == 0 && !pending.empty()) {
      while (issued.size() == 0 && !pending.empty()) {
        memsys.ClockTick();
        ++interface_clock;
        issue();
      }
    }
    if (issued.size() == 0 && pending.empty()) {
      return false;
    }
    return true;
  }
};

} // namespace xerxes
