#pragma once
#include "def.hpp"
#include "device.hpp"

#include <unordered_set>

namespace xerxes {
class Packaging : public Device {
private:
  struct Package {
    size_t pkg_id;
    std::map<PktID, Packet> sub_pkts;
  };
  std::unordered_map<size_t, Package> packages;

  std::set<TopoID> upstreams;
  size_t packaging_num;
  size_t cur_pkg_id = 0;

public:
  Packaging(Topology *topology, size_t packaging_num,
            std::string name = "Packaging")
      : Device(topology, name), packaging_num(packaging_num) {}

  void add_upstream(TopoID host) { upstreams.insert(host); }

  void transit() override {
    auto pkt = receive_pkt();
    auto tick = pkt.arrive;
    Logger::debug() << name_ << " receive packet " << pkt.id << std::endl;
    if (upstreams.find(pkt.from) == upstreams.end()) {
      Logger::debug() << name_ << " directly send packet " << pkt.id
                      << (pkt.is_rsp ? "r" : "") << std::endl;
      send_pkt(pkt);
      return;
    }
    Logger::debug() << name_ << " package packet " << pkt.id
                    << (pkt.is_rsp ? "r" : "") << std::endl;
    auto it = packages.find(cur_pkg_id);
    if (it == packages.end()) {
      auto pkg = Package{};
      pkg.pkg_id = cur_pkg_id;
      pkt.payload *= packaging_num;
      pkg.sub_pkts[pkt.id] = pkt;
      packages[cur_pkg_id] = pkg;
    } else {
      auto &pkg = it->second;
      pkt.is_sub_pkt = true;
      pkt.payload = 0;
      pkg.sub_pkts[pkt.id] = pkt;
    }
    if (packages[cur_pkg_id].sub_pkts.size() == packaging_num) {
      for (auto &sub_pkt : packages[cur_pkg_id].sub_pkts) {
        sub_pkt.second.delta_stat(PACKAGING_DELAY,
                                  (double)(tick > sub_pkt.second.arrive
                                               ? tick - sub_pkt.second.arrive
                                               : 0));
        sub_pkt.second.arrive =
            (tick > sub_pkt.second.arrive ? tick : sub_pkt.second.arrive);
        Logger::debug() << name_ << " send packet " << sub_pkt.second.id
                        << std::endl;
        send_pkt(sub_pkt.second);
      }
      packages.erase(cur_pkg_id++);
    }
  }
};

class BurstHandler : public Device {
private:
  struct Recorder {
    Packet origin;
    std::unordered_set<PktID> sub_pkts;
  };
  std::unordered_map<PktID, Recorder> bursts;
  std::unordered_map<PktID, PktID> reverse;

public:
  BurstHandler(Topology *topology, std::string name = "BurstHandler")
      : Device(topology, name) {}

  void transit() override {
    auto pkt = receive_pkt();
    Logger::debug() << name_ << " receive packet " << pkt.id << std::endl;
    if (pkt.type == INV || pkt.type == CORUPT || pkt.burst <= 1) {
      send_pkt(pkt);
      return;
    }
    if (pkt.burst > 1) {
      auto id = pkt.id;
      if (!pkt.is_rsp) {
        auto it = bursts.find(id);
        ASSERT(it == bursts.end(),
               name_ + " double receiving origin pkt " + std::to_string(id));
        bursts[id] = {pkt, std::unordered_set<PktID>{}};
        auto &pkt = bursts[id].origin;
        for (size_t i = 0; i < pkt.burst; i++) {
          auto new_pkt =
              PktBuilder()
                  .src(pkt.src)
                  .dst(pkt.dst)
                  .addr(pkt.addr + 64 * i)
                  .sent(pkt.sent)
                  .arrive(pkt.arrive)
                  .payload(pkt.payload == 0 ? 0 : 64)
                  .burst(pkt.burst) // Remain the same for filtering the rsp
                  .type(pkt.type)
                  .build();
          bursts[id].sub_pkts.insert(new_pkt.id);
          reverse[new_pkt.id] = id;
          Logger::debug() << name_ << " send sub-packet " << new_pkt.id
                          << std::endl;
          send_pkt(new_pkt);
        }
      } else {
        auto it = reverse.find(id);
        ASSERT(it != reverse.end(),
               name_ + " cannot find recorded sub-pkt " + std::to_string(id));
        auto origin_id = it->second;
        auto &rec = bursts[origin_id];

        rec.origin.delta_stat(DEVICE_PROCESS_TIME,
                              (double)pkt.get_stat(DEVICE_PROCESS_TIME));
        rec.origin.delta_stat(
            DRAM_INTERFACE_QUEUING_DELAY,
            (double)pkt.get_stat(DRAM_INTERFACE_QUEUING_DELAY));
        rec.origin.delta_stat(DRAM_TIME, (double)pkt.get_stat(DRAM_TIME));

        rec.sub_pkts.erase(id);
        if (rec.sub_pkts.empty()) {
          Logger::debug() << name_ << " send origin packet " << rec.origin.id
                          << std::endl;

          rec.origin.delta_stat(WAIT_ALL_BURST,
                                (double)(pkt.arrive - rec.origin.arrive));

          std::swap(rec.origin.src, rec.origin.dst);
          rec.origin.is_rsp = true;
          rec.origin.arrive = pkt.arrive;
          rec.origin.payload =
              rec.origin.is_write() ? 0 : 64 * rec.origin.burst;
          send_pkt(rec.origin);
          bursts.erase(origin_id);
        }
      }
    }
  }
};

} // namespace xerxes
