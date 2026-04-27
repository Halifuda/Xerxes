#pragma once
#ifndef XERXES_ADDRESS_SYSTEM_HH
#define XERXES_ADDRESS_SYSTEM_HH

#include "def.hh"

#include <vector>

namespace xerxes {

class AddressSystem {
public:
    struct Target {
        TopoID dpid;
        Addr dpa_base;
    };

    struct Region {
        Addr hpa_start;
        size_t hpa_size;
        size_t ways;
        size_t granularity;
        std::vector<Target> targets;
    };

    struct QueryResult {
        Addr dpa;
        TopoID dpid;
        bool valid;
    };

    void add_region(const Region &r) { regions_.push_back(r); }

    QueryResult query(Addr hpa) const {
        for (auto &region : regions_) {
            if (hpa >= region.hpa_start &&
                hpa < region.hpa_start + region.hpa_size) {
                Addr offset = hpa - region.hpa_start;
                size_t stripe = offset / region.granularity;
                size_t target_idx = stripe % region.ways;
                Addr target_in_stripe = offset % region.granularity;
                Addr dpa = region.targets[target_idx].dpa_base +
                           (stripe / region.ways) * region.granularity +
                           target_in_stripe;
                return {dpa, region.targets[target_idx].dpid, true};
            }
        }
        return {0, -1, false};
    }

    const std::vector<Region>& regions() const { return regions_; }

private:
    std::vector<Region> regions_;
};

} // namespace xerxes

#endif // XERXES_ADDRESS_SYSTEM_HH
