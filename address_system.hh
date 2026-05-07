#pragma once
#ifndef XERXES_ADDRESS_SYSTEM_HH
#define XERXES_ADDRESS_SYSTEM_HH

#include "def.hh"

#include <cstddef>
#include <vector>

namespace xerxes {

// MemoryNode lists only memory endpoints. Host DPIDs are directly identified
// by TopoID in the fabric and are not part of AddressSystem mapping.
class AddressSystem {
public:
    struct MemoryNode {
        Addr dpa_base;
        TopoID dpid;
    };

    // A group in a grouped-interleave region. Each group has its own set
    // of memory targets interleaved at its own granularity.
    struct Group {
        size_t ways;
        size_t granularity;
        std::vector<MemoryNode> memories;
    };

    // A contiguous HPA range mapped to one or more memory nodes.
    // Flat mode (grouped=false): ways * granularity interleave across memories.
    // Grouped mode: outer-level group_granularity interleave across groups,
    //   inner-level per-group granularity interleave within each group.
    struct Region {
        Addr hpa_start;
        size_t hpa_size;

        bool grouped = false;

        // ---- Flat mode ----
        size_t ways = 0;
        size_t granularity = 64;
        std::vector<MemoryNode> memories;

        // ---- Grouped mode ----
        size_t group_granularity = 0;
        std::vector<Group> groups;
    };

    struct QueryResult {
        Addr dpa;
        TopoID dpid;
        bool valid;
    };

    void add_region(const Region &r) { regions_.push_back(r); }

    QueryResult query(Addr hpa) const {
        for (auto &region : regions_) {
            if (hpa < region.hpa_start ||
                hpa >= region.hpa_start + region.hpa_size)
                continue;

            if (!region.grouped)
                return query_flat(region, hpa);
            else
                return query_grouped(region, hpa);
        }
        return {0, -1, false};
    }

    const std::vector<Region>& regions() const { return regions_; }

private:
    std::vector<Region> regions_;

    static QueryResult query_flat(const Region &r, Addr hpa) {
        Addr offset = hpa - r.hpa_start;
        size_t stripe = offset / r.granularity;
        size_t target_idx = stripe % r.ways;
        Addr target_in_stripe = offset % r.granularity;
        Addr dpa = r.memories[target_idx].dpa_base +
                   (stripe / r.ways) * r.granularity +
                   target_in_stripe;
        return {dpa, r.memories[target_idx].dpid, true};
    }

    static QueryResult query_grouped(const Region &r, Addr hpa) {
        Addr offset = hpa - r.hpa_start;

        size_t group_stripe = offset / r.group_granularity;
        size_t group_idx = group_stripe % r.groups.size();
        const Group &group = r.groups[group_idx];

        size_t full_round_bytes =
            (group_stripe / r.groups.size()) * r.group_granularity;
        Addr inner_offset = full_round_bytes + (offset % r.group_granularity);

        size_t inner_stripe = inner_offset / group.granularity;
        size_t target_idx = inner_stripe % group.ways;

        Addr dpa = group.memories[target_idx].dpa_base +
                   (inner_stripe / group.ways) * group.granularity +
                   (inner_offset % group.granularity);
        return {dpa, group.memories[target_idx].dpid, true};
    }
};

} // namespace xerxes

#endif // XERXES_ADDRESS_SYSTEM_HH
