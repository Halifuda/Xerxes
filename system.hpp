#pragma once
#include "device.hpp"
#include <map>

namespace xerxes {
class System {
  std::map<TopoID, Device *> devices;

public:
  System *add_dev(Device *device) {
    devices.insert({device->id(), device});
    return this;
  }

  Device *find_dev(TopoID id) {
    auto d = devices.find(id);
    if (d == devices.end())
      return nullptr;
    return d->second;
  }
};
} // namespace xerxes
