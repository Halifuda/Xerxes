#pragma once
#ifndef XERXES_TOPOLOGY_HH
#define XERXES_TOPOLOGY_HH

#include "def.hh"

#include <limits>
#include <list>
#include <map>
#include <queue>
#include <set>
#include <vector>

namespace xerxes {

class Switch;

// A node in the topology graph, representing a device in the simulation.
class TopoNode {
    friend class Topology;

    TopoID self;
    std::set<TopoID> neighbors_;
    std::map<TopoID, double> edge_costs_;
    std::list<Packet> buffer;

  public:
    bool receive(Packet &pkt) {
        if (!buffer.empty()) {
            pkt = buffer.front();
            buffer.pop_front();
            return true;
        }
        return false;
    }

    bool send(Packet pkt) {
        buffer.push_back(pkt);
        return true;
    }

    void show_all_pkt() {
        for (auto &pkt : buffer)
            XerxesLogger::debug() << pkt.id << " ";
        XerxesLogger::debug() << std::endl;
    }

    const std::set<TopoID> &neighbors() { return neighbors_; }

    double edge_cost(TopoID neighbor) const {
        auto it = edge_costs_.find(neighbor);
        return (it != edge_costs_.end()) ? it->second : 1.0;
    }

    TopoID id() { return self; }

  private:
    void set_edge_cost(TopoID neighbor, double cost) {
        edge_costs_[neighbor] = cost;
    }
};

// The topology graph.
class Topology {
    std::vector<TopoNode> nodes;
    std::vector<std::vector<TopoID>> router;

  public:
    enum RoutingPolicy { BFS, WEIGHTED, BANDWIDTH_AWARE };

    // Allocate a new device node.
    TopoID new_node() {
        auto node = TopoNode{};
        node.self = nodes.size();
        nodes.push_back(node);
        return nodes.size() - 1;
    }

    // Add a new edge between two nodes.
    Topology *add_edge(TopoID first, TopoID second, double cost = 1.0) {
        if (first < 0 || second < 0 || (size_t)first >= nodes.size() ||
            (size_t)second >= nodes.size())
            return this;
        nodes[first].neighbors_.insert(second);
        nodes[second].neighbors_.insert(first);
        nodes[first].set_edge_cost(second, cost);
        nodes[second].set_edge_cost(first, cost);
        return this;
    }

    void set_edge_cost(TopoID first, TopoID second, double cost) {
        if (first >= 0 && second >= 0 && (size_t)first < nodes.size() &&
            (size_t)second < nodes.size()) {
            nodes[first].set_edge_cost(second, cost);
            nodes[second].set_edge_cost(first, cost);
        }
    }

    double edge_cost(TopoID from, TopoID to) const {
        if (from < 0 || to < 0 || (size_t)from >= nodes.size() ||
            (size_t)to >= nodes.size())
            return std::numeric_limits<double>::infinity();
        return nodes[from].edge_cost(to);
    }

    // Build the default routing table.
    void build_route() {
        router.resize(nodes.size());
        for (auto &r : router)
            r.resize(nodes.size(), -1);

        struct BFSEntry {
            TopoID from_neighbor;
            TopoID cur;
        };

        for (size_t i = 0; i < nodes.size(); i++) {
            std::queue<BFSEntry> q;
            std::set<TopoID> visited;
            for (auto &neighbor : nodes[i].neighbors_) {
                router[i][neighbor] = neighbor;
                q.push({neighbor, neighbor});
            }
            while (!q.empty()) {
                auto entry = q.front();
                q.pop();
                if (visited.find(entry.cur) != visited.end())
                    continue;
                visited.insert(entry.cur);
                for (auto &next : nodes[entry.cur].neighbors_) {
                    if (visited.find(next) == visited.end()) {
                        router[i][next] = entry.from_neighbor;
                        q.push({entry.from_neighbor, next});
                    }
                }
            }
        }
        // TODO: -1 or self?
        for (size_t i = 0; i < nodes.size(); i++)
            router[i][i] = -1;
    }

    void log_route(std::ostream &os) {
        for (size_t i = 0; i < nodes.size(); i++) {
            for (size_t j = 0; j < nodes.size(); j++) {
                if (router[i][j] == -1)
                    continue;
                os << i << " -> " << j << " : " << router[i][j] << std::endl;
            }
        }
    }

    TopoNode *get_node(TopoID id) {
        if (id < 0 || (size_t)id >= nodes.size())
            return nullptr;
        return &nodes[id];
    }

    TopoNode *next_node(TopoID from, TopoID to) {
        if (from < 0 || to < 0 || (size_t)from >= nodes.size() ||
            (size_t)to >= nodes.size())
            return nullptr;
        if (router[from][to] == -1)
            return nullptr;
        return &nodes[router[from][to]];
    }

    void build_pbr_routes(
        const std::vector<std::pair<TopoID, Switch*>>& switches,
        const std::vector<TopoID>& endpoints,
        RoutingPolicy policy = BFS
    );
};

} // namespace xerxes

#endif // XERXES_TOPOLOGY_HH
