#include "xerxes_standalone.hh"
#include "address_system.hh"
#include "bus.hh"
#include "def.hh"
#include "device.hh"
#include "dramsim3_interface.hh"
#include "requester.hh"
#include "snoop.hh"
#include "switch.hh"
#include "topology.hh"
#include "utils.hh"
#include <functional>
#include <limits>
#include <sstream>
#include <utility>

#include "ext/toml.hpp"

namespace xerxes {

void Topology::build_pbr_routes(
    const std::vector<std::pair<TopoID, Switch*>>& switches,
    const std::vector<TopoID>& endpoints,
    RoutingPolicy policy) {

    if (policy == BFS) {
        for (auto &ep_id : endpoints) {
            std::queue<TopoID> q;
            std::map<TopoID, TopoID> parent;
            q.push(ep_id);
            while (!q.empty()) {
                auto cur = q.front(); q.pop();
                auto node = get_node(cur);
                if (!node) continue;
                for (auto &neighbor : node->neighbors()) {
                    if (parent.find(neighbor) == parent.end() && neighbor != ep_id) {
                        parent[neighbor] = cur;
                        q.push(neighbor);
                    }
                }
            }
            for (auto &sw_ref : switches) {
                auto it = parent.find(sw_ref.first);
                if (it != parent.end())
                    sw_ref.second->set_pbr_route(ep_id, it->second);
            }
        }
        return;
    }

    // WEIGHTED / BANDWIDTH_AWARE: Dijkstra from each endpoint outward.
    for (auto &ep_id : endpoints) {
        std::map<TopoID, double> dist;
        std::map<TopoID, TopoID> parent;

        using PQEntry = std::pair<double, TopoID>;
        std::priority_queue<PQEntry, std::vector<PQEntry>,
                            std::greater<PQEntry>> pq;

        dist[ep_id] = 0;
        pq.push({0, ep_id});

        while (!pq.empty()) {
            auto [cur_dist, cur] = pq.top();
            pq.pop();
            if (cur_dist > dist[cur])
                continue;

            auto node = get_node(cur);
            if (!node)
                continue;

            for (auto &neighbor : node->neighbors()) {
                if (neighbor == ep_id)
                    continue;
                double ew = edge_cost(cur, neighbor);
                if (policy == BANDWIDTH_AWARE)
                    ew = (ew > 0) ? (1.0 / ew) : std::numeric_limits<double>::max();

                double new_dist = cur_dist + ew;
                auto it = dist.find(neighbor);
                if (it == dist.end() || new_dist < it->second) {
                    dist[neighbor] = new_dist;
                    parent[neighbor] = cur;
                    pq.push({new_dist, neighbor});
                }
            }
        }

        for (auto &sw_ref : switches) {
            auto it = parent.find(sw_ref.first);
            if (it != parent.end())
                sw_ref.second->set_pbr_route(ep_id, it->second);
        }
    }
}
Simulation *glb_sim = nullptr;
std::vector<std::function<void(std::ostream &)>> glb_stat_loggers;
std::vector<std::function<void()>> glb_stat_summarizers;

void default_logger(const Packet &pkt) {
    static bool first = true;
    if (first) {
        first = false;
        XerxesLogger::info()
            << "id,type,memid,addr,send,arrive,bus_queuing,bus_time,"
               "switch_queuing,switch_time,snoop_evict,host_inv,"
               "dram_queuing,dram_time,total_time"
            << std::endl;
    }
    XerxesLogger::info() << pkt.id << "," << TypeName::of(pkt.type) << ","
                         << pkt.src << "," << std::hex << pkt.addr << std::dec
                         << "," << pkt.sent << "," << pkt.arrive << ","
                         << pkt.get_stat(NormalStatType::BUS_QUEUE_DELAY) << ","
                         << pkt.get_stat(NormalStatType::BUS_TIME) << ","
                         << pkt.get_stat(NormalStatType::SWITCH_QUEUE_DELAY)
                         << "," << pkt.get_stat(NormalStatType::SWITCH_TIME)
                         << ","
                         << pkt.get_stat(NormalStatType::SNOOP_EVICT_DELAY)
                         << "," << pkt.get_stat(NormalStatType::HOST_INV_DELAY)
                         << ","
                         << pkt.get_stat(
                                NormalStatType::DRAM_INTERFACE_QUEUING_DELAY)
                         << "," << pkt.get_stat(NormalStatType::DRAM_TIME)
                         << "," << pkt.arrive - pkt.sent << std::endl;
}

void init_sim(Simulation *sim) { glb_sim = sim; }

void set_pkt_logger(std::ostream &os, XerxesLogLevel level,
                    Packet::XerxesLoggerFunc pkt_logger) {
    XerxesLogger::set(os, level);
    Packet::pkt_logger(true, pkt_logger);
}

class EventEngine {
    std::multimap<Tick, EventFunc> events;

  public:
    EventEngine() {}

    static EventEngine *glb(EventEngine *n = nullptr) {
        static EventEngine *engine = nullptr;
        if (n != nullptr)
            engine = n;
        return engine;
    }

    void add(Tick tick, EventFunc f) { events.insert(std::make_pair(tick, f)); }

    Tick step() {
        if (!events.empty()) {
            auto tick = events.begin()->first;
            auto event = events.begin()->second;
            events.erase(events.begin());
            event();
            return tick;
        }
        return 0;
    }

    bool empty() { return events.empty(); }
} glb_engine;

void Device::sched_transit(Tick tick) {
    glb_engine.add(tick, [this]() { this->transit(); });
}

void xerxes_schedule(EventFunc f, uint64_t tick) { glb_engine.add(tick, f); }

bool xerxes_events_empty() { return glb_engine.empty(); }

Tick step() { return glb_engine.step(); }

bool events_empty() { return glb_engine.empty(); }

#define BUILD_DEVICE(TypeName, ConfigType)                                     \
    else if (type == #TypeName) {                                              \
        auto config =                                                          \
            toml::find_or<ConfigType>(data, pair.first, ConfigType{});         \
        auto dev = new TypeName(glb_sim, config, pair.first);                  \
        glb_sim->system()->add_dev(dev);                                       \
        glb_stat_summarizers.push_back(                                        \
            [dev]() { dev->collect_summary(); });                              \
        glb_stat_loggers.push_back(                                            \
            [dev](std::ostream &os) { dev->log_stats(os); });                  \
        if (type == "Requester")                                               \
            ctx.requesters.push_back(dynamic_cast<Requester *>(dev));          \
        else if (type == "DRAMsim3Interface")                                  \
            ctx.mems.push_back(dynamic_cast<DRAMsim3Interface *>(dev));        \
        auto id = dev->id();                                                   \
        if (type == "Switch")                                                  \
            ctx.switches.push_back({id, dynamic_cast<Switch *>(dev)});         \
        else if (type == "Snoop")                                              \
            ctx.snoops.push_back(dynamic_cast<Snoop *>(dev));                  \
        ctx.name_to_id[pair.first] = id;                                       \
        XerxesLogger::debug()                                                  \
            << "Add " #TypeName ": " << pair.first << "#" << id << std::endl;  \
    }

XerxesContext parse_config(std::string config_file_name) {
    ASSERT(glb_sim != nullptr, "Simulation is not initialized.");
    glb_stat_loggers.clear();
    glb_stat_summarizers.clear();
    XerxesContext ctx;
    auto data = toml::parse(config_file_name);
    ctx.general = toml::get<XerxesConfig>(data);

    auto rp = toml::find_or(data, "routing_policy", std::string("bfs"));
    if (rp == "weighted")
        ctx.routing_policy = Topology::WEIGHTED;
    else if (rp == "bandwidth_aware")
        ctx.routing_policy = Topology::BANDWIDTH_AWARE;
    else
        ctx.routing_policy = Topology::BFS;

    for (auto &pair : ctx.general.devices) {
        auto type = pair.second;
        if (type == "SthUknown") {
            PANIC("Unknown device type: " + type);
        }
        BUILD_DEVICE(Requester, RequesterConfig)
        BUILD_DEVICE(Switch, SwitchConfig)
        BUILD_DEVICE(DuplexBus, DuplexBusConfig)
        BUILD_DEVICE(DRAMsim3Interface, DRAMsim3InterfaceConfig)
        BUILD_DEVICE(Snoop, SnoopConfig)
        else {
            PANIC("Unknown device type: " + type);
        }
    }
    for (auto &pair : ctx.general.edges) {
        auto from = ctx.name_to_id[pair.first];
        auto to = ctx.name_to_id[pair.second];
        glb_sim->topology()->add_edge(from, to);
    }
    auto edge_costs = toml::find_or(data, "edge_costs",
                                    std::vector<EdgeCostEntry>{});
    for (auto &ec : edge_costs) {
        auto from = ctx.name_to_id[ec.from];
        auto to = ctx.name_to_id[ec.to];
        glb_sim->topology()->set_edge_cost(from, to, ec.cost);
    }
    glb_sim->topology()->build_route();

    auto as = new AddressSystem();

    auto has_as = data.contains("address_system");
    if (!has_as) {
        AddressSystem::Region region;
        region.hpa_start = 0;
        region.hpa_size = 1ULL << 30;
        region.ways = ctx.mems.size();
        region.granularity = 64;
        for (size_t i = 0; i < ctx.mems.size(); ++i)
            region.memories.push_back({0, ctx.mems[i]->id()});
        as->add_region(region);
    } else {
        auto as_data = toml::find<toml::value>(data, "address_system");
        auto grouped = toml::find_or(as_data, "grouped", false);

        if (grouped) {
            AddressSystem::Region region;
            region.hpa_start =
                toml::find_or(as_data, "hpa_start", (Addr)0);
            region.hpa_size =
                toml::find_or(as_data, "hpa_size", (size_t)(1ULL << 30));
            region.grouped = true;
            region.group_granularity =
                toml::find_or(as_data, "group_granularity", (size_t)256);

            auto groups_data = toml::find<std::vector<toml::value>>(
                as_data, "groups");
            for (auto &gv : groups_data) {
                AddressSystem::Group group;
                group.ways = toml::find_or(gv, "ways", (size_t)1);
                group.granularity =
                    toml::find_or(gv, "granularity", (size_t)64);
                auto mem_names =
                    toml::find<std::vector<std::string>>(gv, "memories");
                for (auto &name : mem_names)
                    group.memories.push_back({0, ctx.name_to_id[name]});
                region.groups.push_back(group);
            }
            as->add_region(region);
        } else {
            AddressSystem::Region region;
            region.hpa_start =
                toml::find_or(as_data, "hpa_start", (Addr)0);
            region.hpa_size =
                toml::find_or(as_data, "hpa_size", (size_t)(1ULL << 30));
            region.ways =
                toml::find_or(as_data, "ways", ctx.mems.size());
            region.granularity =
                toml::find_or(as_data, "granularity", (size_t)64);
            auto mem_names = toml::find_or(
                as_data, "memories", std::vector<std::string>{});
            if (mem_names.empty()) {
                for (size_t i = 0; i < ctx.mems.size(); ++i)
                    region.memories.push_back({0, ctx.mems[i]->id()});
            } else {
                for (auto &name : mem_names)
                    region.memories.push_back(
                        {0, ctx.name_to_id[name]});
            }
            as->add_region(region);
        }
    }
    glb_sim->set_address_system(as);

    for (auto *req : ctx.requesters)
        ctx.endpoint_ids.push_back(req->id());
    for (auto *mem : ctx.mems)
        ctx.endpoint_ids.push_back(mem->id());
    for (auto *snoop : ctx.snoops)
        ctx.endpoint_ids.push_back(snoop->id());
    glb_sim->topology()->build_pbr_routes(
        ctx.switches,
        ctx.endpoint_ids,
        ctx.routing_policy
    );

    for (auto &req : ctx.requesters) {
        req->set_hpa_range(0, 1ULL << 30);
    }
    return ctx;
}

void log_summary(std::ostream &os) {
    auto &s = glb_sim->system()->summary;
    if (s.empty()) return;
    os << "metric,value" << std::endl;
    for (auto &e : s) {
        os << e.first << "," << e.second << std::endl;
    }
}

void log_stats(std::ostream &os) {
    for (auto &summarizer : glb_stat_summarizers)
        summarizer();
    log_summary(os);
    for (auto &logger : glb_stat_loggers)
        logger(os);
}
} // namespace xerxes
