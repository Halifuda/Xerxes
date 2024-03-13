#pragma once

#include "def.hpp"
#include "utils.hpp"

#include <iostream>
#include <queue>
#include <set>

namespace xerxes {
class TopoNode {
  friend class Topology;

  TopoID self;
  std::set<TopoID> neighbors;
  std::queue<Packet> buffer;

public:
  bool receive(Packet &pkt) {
    // TODO
    if (!buffer.empty()) {
      pkt = buffer.front();
      buffer.pop();
      return true;
    }
    return false;
  }

  bool send(Packet pkt) {
    auto tick = pkt.arrive;
    buffer.push(pkt);
    Notifier::glb()->add(tick, (void *)(this->self));
    return true;
  }

  TopoID id() { return self; }
  std::set<TopoID> &neighbor_set() { return neighbors; }
};

class Topology {
  std::vector<TopoNode> nodes;
  std::vector<std::vector<TopoID>> router;

public:
  TopoID new_node() {
    auto node = TopoNode{};
    node.self = nodes.size();
    nodes.push_back(node);
    return nodes.size() - 1;
  }

  Topology *add_edge(TopoID first, TopoID second) {
    if (first < 0 || second < 0 || (size_t)first >= nodes.size() ||
        (size_t)second >= nodes.size())
      return this;
    nodes[first].neighbors.insert(second);
    nodes[second].neighbors.insert(first);
    return this;
  }

  void build_route() {
    router.resize(nodes.size());
    for (auto &r : router)
      r.resize(nodes.size(), -1);

    for (size_t i = 0; i < nodes.size(); i++) {
      for (auto &n : nodes[i].neighbors)
        router[i][n] = n;

      for (auto &n : nodes[i].neighbors) {
        // make the router, i -> n -> x, router[i][x] = n
        std::queue<TopoID> q;
        std::set<TopoID> visited;
        q.push(n);
        visited.insert(n);
        while (!q.empty()) {
          auto cur = q.front();
          q.pop();
          for (auto &nn : nodes[cur].neighbors) {
            if (visited.find(nn) == visited.end()) {
              visited.insert(nn);
              if (router[i][nn] == -1) {
                q.push(nn);
                router[i][nn] = n;
              }
            }
          }
        }
      }
    }
    // TODO: -1 or self?
    for (size_t i = 0; i < nodes.size(); i++)
      router[i][i] = -1;
  }

  void log_route(Logger &logger) {
    for (size_t i = 0; i < nodes.size(); i++) {
      for (size_t j = 0; j < nodes.size(); j++) {
        if (router[i][j] == -1)
          continue;
        logger << i << " -> " << j << " : " << router[i][j] << std::endl;
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
};
} // namespace xerxes
