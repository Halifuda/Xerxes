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
#include <sstream>
#include <utility>

#include "ext/toml.hpp"

namespace xerxes {

void Topology::build_pbr_routes(
    const std::vector<std::pair<TopoID, Switch*>>& switches,
    const std::vector<TopoID>& endpoints,
    RoutingPolicy policy) {

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
    glb_sim->topology()->build_route();

    auto as = new AddressSystem();
    AddressSystem::Region region;
    region.hpa_start = 0;
    region.hpa_size = 1ULL << 30;
    region.ways = ctx.mems.size();
    region.granularity = 64;
    for (size_t i = 0; i < ctx.mems.size(); ++i) {
        region.memories.push_back({0, ctx.mems[i]->id()});
    }
    as->add_region(region);
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
