#pragma once
#ifndef XERXES_SYSTEM_HH
#define XERXES_SYSTEM_HH

#include "def.hh"

namespace xerxes {
class System {
    std::map<TopoID, Device *> devices;

  public:
    System *add_dev(Device *device);
    Device *find_dev(TopoID id);
};
} // namespace xerxes

#endif // XERXES_SYSTEM_HH
