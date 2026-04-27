#pragma once
#ifndef XERXES_FABRIC_MANAGER_HH
#define XERXES_FABRIC_MANAGER_HH

#include "def.hh"
#include "topology.hh"
#include "switch.hh"
#include <map>
#include <queue>
#include <set>
#include <vector>

namespace xerxes {

class FabricManager {
public:
    struct EndpointRef {
        TopoID id;
    };

    struct SwitchRef {
        TopoID id;
        Switch* sw;
    };

    enum Policy { BFS, WEIGHTED };

    inline static void build_routes(
        Topology* topo,
        const std::vector<SwitchRef>& switches,
        const std::vector<EndpointRef>& endpoints,
        Policy policy = BFS
    );
};

inline void FabricManager::build_routes(
    Topology* topo,
    const std::vector<SwitchRef>& switches,
    const std::vector<EndpointRef>& endpoints,
    Policy policy) {

    std::set<TopoID> switch_ids;
    for (auto &sw : switches) switch_ids.insert(sw.id);

    for (auto &ep : endpoints) {
        std::queue<TopoID> q;
        std::map<TopoID, TopoID> parent;
        q.push(ep.id);
        while (!q.empty()) {
            auto cur = q.front(); q.pop();
            auto node = topo->get_node(cur);
            if (!node) continue;
            for (auto &neighbor : node->neighbors()) {
                if (parent.find(neighbor) == parent.end() && neighbor != ep.id) {
                    parent[neighbor] = cur;
                    q.push(neighbor);
                }
            }
        }
        for (auto &sw : switches) {
            auto it = parent.find(sw.id);
            if (it != parent.end()) {
                sw.sw->set_pbr_route(ep.id, it->second);
            }
        }
    }
}

} // namespace xerxes

#endif // XERXES_FABRIC_MANAGER_HH
