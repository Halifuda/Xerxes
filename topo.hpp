#pragma once

#include "def.hpp"
#include "utils.hpp"

#include <iostream>
#include <list>
#include <queue>
#include <set>

namespace xerxes {
class TopoNode {
  friend class Topology;

  TopoID self;
  std::set<TopoID> neighbors_;
  std::list<Packet> buffer;

public:
  bool receive(Packet &pkt) {
    // TODO
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
      Logger::debug() << pkt.id << " ";
    Logger::debug() << std::endl;
  }

  const std::set<TopoID> &neighbors() { return neighbors_; }

  TopoID id() { return self; }
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
    nodes[first].neighbors_.insert(second);
    nodes[second].neighbors_.insert(first);
    return this;
  }

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
};
} // namespace xerxes
