#pragma once
#ifndef XERXES_SYSTEM_HH
#define XERXES_SYSTEM_HH

#include "def.hh"

#include <map>

namespace xerxes {
class System {
    std::map<TopoID, Device *> devices;

  public:
    std::map<std::string, double> summary;

    void add_summary(const std::string &key, double val) {
        if (summary.find(key) != summary.end())
            summary[key] += val;
        else
            summary[key] = val;
    }

    System *add_dev(Device *device);
    Device *find_dev(TopoID id);
};
} // namespace xerxes

#endif // XERXES_SYSTEM_HH
