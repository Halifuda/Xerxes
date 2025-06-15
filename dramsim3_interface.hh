#pragma once
#ifndef XERXES_DRAMSIM3_INTERFACE_HH
#define XERXES_DRAMSIM3_INTERFACE_HH

#include "DRAMsim3/src/memory_system.h"
#include "device.hh"
#include "utils.hh"

#include <list>
#include <map>

namespace xerxes {

class DRAMsim3InterfaceConfig {
  public:
    Tick tick_per_clock = 1;
    Tick process_time = 1;
    Addr start = 0;
    size_t capacity = 1 << 30;
    double wr_ratio = 0.5;
    std::string config_file = "DRAMsim3/configs/DDR4_8Gb_x8_3200.ini";
    std::string output_dir = "output";
};
} // namespace xerxes
TOML11_DEFINE_CONVERSION_NON_INTRUSIVE(xerxes::DRAMsim3InterfaceConfig,
                                       tick_per_clock, process_time, start,
                                       capacity, wr_ratio, config_file,
                                       output_dir);
namespace xerxes {
class DRAMsim3Interface : public Device {
  private:
    Addr start;
    size_t capa;
    double ratio;
    std::vector<Packet> pending;
    std::map<Addr, std::list<Packet>> issued;

    Tick tick_per_clock;
    Tick interface_clock = 0;
    Tick process_time;

    dramsim3::MemorySystem memsys;

    // Issue packets to the DRAM system.
    void issue() {
        std::vector<std::vector<Packet>::iterator> to_erase;
        // Try to issue all pending packets.
        for (auto it = pending.begin(); it != pending.end(); ++it) {
            auto &pkt = *it;
            // Tick to the packet arrival
            while ((interface_clock * tick_per_clock) < pkt.arrive) {
                memsys.ClockTick();
                ++interface_clock;
            }
            if (memsys.WillAcceptTransaction(pkt.addr - start,
                                             pkt.is_write())) {
                if (issued.find(pkt.addr) == issued.end())
                    issued[pkt.addr] = std::list<Packet>();
                issued[pkt.addr].push_back(pkt);
                to_erase.push_back(it);
                if (interface_clock * tick_per_clock > pkt.arrive) {
                    pkt.delta_stat(DRAM_INTERFACE_QUEUING_DELAY,
                                   (double)(interface_clock * tick_per_clock -
                                            pkt.arrive));
                    pkt.arrive = interface_clock * tick_per_clock;
                }
                memsys.AddTransaction(pkt.addr - start, pkt.is_write());
            }
        }
        for (auto it : to_erase) {
            pending.erase(it);
        }
    }

  public:
    DRAMsim3Interface(Simulation *sim, const DRAMsim3InterfaceConfig &config,
                      std::string name = "DRAMsim3Interface")
        : Device(sim, name), start(config.start), capa(config.capacity),
          ratio(config.wr_ratio), tick_per_clock(config.tick_per_clock),
          process_time(config.process_time),
          memsys(config.config_file, config.output_dir,
                 std::bind(&DRAMsim3Interface::callback, this,
                           std::placeholders::_1),
                 std::bind(&DRAMsim3Interface::callback, this,
                           std::placeholders::_1)) {}

    Addr start_addr() const { return start; }
    size_t capacity() const { return capa; }
    double wr_ratio() const { return ratio; }

    void transit() override {
        auto pkt = receive_pkt();
        while (pkt.type != PacketType::PKT_TYPE_NUM) {
            if (pkt.dst == self) {
                XerxesLogger::debug()
                    << name() << " receive packet " << pkt.id << " from "
                    << pkt.from << " at " << pkt.arrive << std::endl;
                pkt.delta_stat(DEVICE_PROCESS_TIME, (double)(process_time));
                pkt.arrive += process_time;
                pending.push_back(pkt);
            } else {
                send_pkt(pkt);
            }
            pkt = receive_pkt();
        }
        return issue();
    }

    // Callback function called by DRAMsim3 when a packet is completed.
    void callback(Addr addr) {
        auto it = issued.find(addr + start);
        if (it == issued.end())
            return;
        auto &pkt = it->second.front();
        XerxesLogger::debug() << "Callback #" << pkt.id << "r at "
                              << interface_clock * tick_per_clock << std::endl;
        std::swap(pkt.src, pkt.dst);

        // TODO: is the callback called at the exact tick?
        pkt.delta_stat(DRAM_TIME,
                       (double)(interface_clock * tick_per_clock - pkt.arrive));
        pkt.arrive = interface_clock * tick_per_clock;
        pkt.is_rsp = true;
        if (pkt.is_write())
            pkt.payload = 0;
        else
            pkt.payload = 64;
        send_pkt(pkt);

        it->second.pop_front();
        if (it->second.empty())
            issued.erase(it); // Save memory
    }

    Tick clock() {
        auto num = issued.size();
        if (num == 0) {
            if (!pending.empty())
                issue();
            return interface_clock * tick_per_clock;
        }
        memsys.ClockTick();
        ++interface_clock;
        if (num != issued.size() && !pending.empty())
            issue();
        return interface_clock * tick_per_clock;
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

#endif // XERXES_DRAMSIM3_INTERFACE_HH
