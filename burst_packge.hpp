#pragma once
#include "def.hpp"
#include "device.hpp"

namespace xerxes {
class Packaging : public Device {
private:
  struct Package {
    size_t pkg_id;
    std::map<PktID, Packet> sub_pkts;
  };
  std::unordered_map<size_t, Package> packages;

  size_t packaging_num;
  size_t cur_pkg_id = 0;

public:
  Packaging(Topology *topology, size_t packaging_num,
            std::string name = "Packaging")
      : Device(topology, name), packaging_num(packaging_num) {}

  void transit() override {
    auto pkt = receive_pkt();
    Logger::debug() << name_ << " receive packet " << pkt.id << std::endl;
    auto it = packages.find(cur_pkg_id);
    if (it == packages.end()) {
      auto pkg = Package{};
      pkg.pkg_id = cur_pkg_id;
      pkg.sub_pkts[pkt.id] = pkt;
      packages[cur_pkg_id] = pkg;
    } else {
      auto &pkg = it->second;
      pkt.is_sub_pkt = true;
      pkg.sub_pkts[pkt.id] = pkt;
    }
    if (packages[cur_pkg_id].sub_pkts.size() == packaging_num) {
      for (auto &sub_pkt : packages[cur_pkg_id].sub_pkts) {
        sub_pkt.second.delta_stat(
            PACKAGING_DELAY, (double)(pkt.arrive > sub_pkt.second.arrive
                                          ? pkt.arrive - sub_pkt.second.arrive
                                          : 0));
        Logger::debug() << name_ << " send packet " << sub_pkt.second.id
                        << std::endl;
        send_pkt(sub_pkt.second);
      }
      packages.erase(cur_pkg_id++);
    }
  }
};

} // namespace xerxes
