#pragma once
#include "utils.hpp"

#include <climits>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>

namespace xerxes {
typedef uint64_t Addr;
typedef int64_t TopoID;
typedef int64_t PktID;
typedef uint64_t Tick;

typedef enum {
  RD,     /* Read (coherent) */
  NT_RD,  /* Non-temporal read */
  WT,     /* Write (coherent) */
  NT_WT,  /* Non-temporal write */
  INV,    /* Invalidate (to Host) */
  CORUPT, /* Corrupted packet */
  PKT_TYPE_NUM
} PacketType;

class TypeName {
public:
  static std::string of(PacketType type) {
    switch (type) {
    case RD:
      return std::string("Read");
    case NT_RD:
      return std::string("Non-temporal read");
    case WT:
      return std::string("Write");
    case NT_WT:
      return std::string("Non-temporal write");
    case INV:
      return std::string("Invalidate");
    case CORUPT:
      return std::string("*Corruptted*");
    default:
      return std::string("*unknown packet type*");
    }
  }
};

typedef enum {
  BUS_QUEUE_DELAY,
  BUS_TIME,
  FRAMING_TIME,
  SWITCH_QUEUE_DELAY,
  SWITCH_TIME,
  PACKAGING_DELAY,
  WAIT_ALL_BURST,
  SNOOP_EVICT_DELAY,
  HOST_INV_DELAY,
  DRAM_INTERFACE_QUEUING_DELAY,
  DEVICE_PROCESS_TIME,
  DRAM_TIME,
  NUM_STATS
} NormalStatType;

class StatKeys {
public:
  StatKeys() {}
  static std::string key_name(NormalStatType type) {
    switch (type) {
    case BUS_QUEUE_DELAY:
      return std::string("bus queue delay");
    case SWITCH_QUEUE_DELAY:
      return std::string("switch queue delay");
    case BUS_TIME:
      return std::string("bus time");
    case FRAMING_TIME:
      return std::string("framing time");
    case SWITCH_TIME:
      return std::string("switch time");
    case PACKAGING_DELAY:
      return std::string("packaging delay");
    case WAIT_ALL_BURST:
      return std::string("wait all burst");
    case SNOOP_EVICT_DELAY:
      return std::string("snoop evict delay");
    case HOST_INV_DELAY:
      return std::string("host inv delay");
    case DRAM_INTERFACE_QUEUING_DELAY:
      return std::string("dram interface queuing delay");
    case DEVICE_PROCESS_TIME:
      return std::string("device process time");
    case DRAM_TIME:
      return std::string("dram time");
    default:
      return std::string("unknown stat type");
    }
  }
};

/**
 * @brief A global table to store packet statistics.
 */
class PktStatsTable {
private:
  PktStatsTable() {}
  typedef std::unordered_map<NormalStatType, double> Table;
  typedef std::unordered_map<PktID, Table> Tables;

public:
  static Tables &get() {
    static Tables table = Tables{};
    return table;
  }
};

struct Packet {
  PktID id;        /* Packet ID */
  PacketType type; /* Packet type */
  Addr addr;       /* Address */
  size_t payload;  /* Payload size (in bytes) */
  size_t burst;    /* Burst size */
  Tick sent;       /* Sent time */
  Tick arrive;     /* Arrive time */
  TopoID from;     /* From (on-trans) */
  TopoID src;      /* Source device */
  TopoID dst;      /* Destination device */
  bool is_rsp;     /* Is response */
  bool is_sub_pkt; /* Is sub-packet, uses 0 time in bus (packaged by former) */

  Packet()
      : id(-1), type(PKT_TYPE_NUM), addr(0), payload(0), burst(1), sent(0),
        arrive(0), from(-1), src(-1), dst(-1), is_rsp(false),
        is_sub_pkt(false) {}
  Packet(PktID id, PacketType type, Addr addr, size_t size, size_t burst,
         Tick sent, Tick arrive, TopoID from, TopoID src, TopoID dst,
         bool is_rsp, bool is_sub_pkt)
      : id(id), type(type), addr(addr), payload(size), burst(burst), sent(sent),
        arrive(std::max(sent, arrive)), from(from), src(src), dst(dst),
        is_rsp(is_rsp), is_sub_pkt(is_sub_pkt) {}
  Packet(const Packet &pkt)
      : id(pkt.id), type(pkt.type), addr(pkt.addr), payload(pkt.payload),
        burst(pkt.burst), sent(pkt.sent), arrive(pkt.arrive), from(pkt.from),
        src(pkt.src), dst(pkt.dst), is_rsp(pkt.is_rsp),
        is_sub_pkt(pkt.is_sub_pkt) {}

  bool valid() { return id != -1 && type != PKT_TYPE_NUM && type != CORUPT; }
  /**
   * @brief Check if the packet is a write request.
   * @return true if the packet is a write request.
   */
  bool is_write() { return type == WT || type == NT_WT; }
  bool is_read() { return type == RD || type == NT_RD; }
  /**
   * @brief Check if the packet is a coherent request (RD/WT).
   * @return true if the packet is a coherent request (RD/WT).
   */
  bool is_coherent() { return type == RD || type == WT; }

  bool has_stat(NormalStatType key) const {
    auto &stats = PktStatsTable::get()[id];
    return stats.find(key) != stats.end();
  }
  double get_stat(NormalStatType key) const {
    auto &stats = PktStatsTable::get()[id];
    return stats[key];
  }
  void set_stat(NormalStatType key, double value) {
    auto &stats = PktStatsTable::get()[id];
    stats.insert(std::make_pair(key, value));
  }
  /**
   * @brief Add on or insert a value `v` to a statistic `s`. Do `s += v`.
   * @param key The key of the statistic.
   * @param value The value.
   */
  void delta_stat(NormalStatType key, double value) {
    auto &stats = PktStatsTable::get()[id];
    Logger::debug() << "delta stat \"" << StatKeys::key_name(key)
                    << "\" = " << value << std::endl;
    if (has_stat(key))
      stats[key] += value;
    else
      stats.insert(std::make_pair(key, value));
  }
  typedef std::function<void(const Packet &)> LoggerFunc;
  static LoggerFunc &pkt_logger(
      bool set = false, LoggerFunc logger = [](const Packet &) {}) {
    static LoggerFunc f = [](const Packet &) {};
    if (set)
      f = logger;
    return f;
  }
  void log_stat() { Packet::pkt_logger()(*this); }
};

class PktBuilder {
private:
  PktID id_i;
  PacketType type_i;
  Addr addr_i;
  size_t payload_i;
  size_t burst_i;
  Tick sent_i;
  Tick arrive_i;
  TopoID from_i;
  TopoID src_i;
  TopoID dst_i;
  bool is_rsp_i;
  bool is_sub_pkt_i;

public:
  PktBuilder()
      : type_i(PKT_TYPE_NUM), addr_i(0), payload_i(0), burst_i(1), sent_i(0),
        arrive_i(0), from_i(-1), src_i(-1), dst_i(-1), is_rsp_i(false),
        is_sub_pkt_i(false) {
    // Automate the packet ID
    static PktID id = 0;
    id_i = id++;
    PktStatsTable::get().insert(
        std::make_pair(id_i, std::unordered_map<NormalStatType, double>{}));
  }

  PktBuilder &type(PacketType type) {
    type_i = type;
    return *this;
  }
  PktBuilder &addr(Addr addr) {
    addr_i = addr;
    return *this;
  }
  PktBuilder &payload(size_t payload) {
    payload_i = payload;
    return *this;
  }
  PktBuilder &burst(size_t burst) {
    burst_i = burst;
    return *this;
  }
  PktBuilder &sent(Tick sent) {
    sent_i = sent;
    arrive_i = std::max(sent_i, arrive_i);
    return *this;
  }
  PktBuilder &arrive(Tick arrive) {
    arrive_i = std::max(sent_i, arrive);
    return *this;
  }
  PktBuilder &src(TopoID src) {
    src_i = src;
    from_i = src;
    return *this;
  }
  PktBuilder &dst(TopoID dst) {
    dst_i = dst;
    return *this;
  }
  PktBuilder &is_rsp(bool is_rsp) {
    is_rsp_i = is_rsp;
    return *this;
  }
  PktBuilder &is_sub_pkt(bool is_sub_pkt) {
    is_sub_pkt_i = is_sub_pkt;
    return *this;
  }
  Packet build() {
    return Packet(id_i, type_i, addr_i, payload_i, burst_i, sent_i, arrive_i,
                  from_i, src_i, dst_i, is_rsp_i, is_sub_pkt_i);
  }
};

typedef std::function<void(void *)> NotifierFunc;

class Notifier {
  std::multimap<uint64_t, void *> notifiers;
  NotifierFunc func;

public:
  Notifier(NotifierFunc f = [](void *) {}) : func(f) {}

  static Notifier *glb(Notifier *n = nullptr) {
    static Notifier *notifier = nullptr;
    if (n != nullptr)
      notifier = n;
    return notifier;
  }

  void set(NotifierFunc f) { func = f; }

  void add(uint64_t tick, void *ptr) {
    notifiers.insert(std::make_pair(tick, ptr));
  }

  uint64_t step() {
    if (!notifiers.empty()) {
      auto tick = notifiers.begin()->first;
      Logger::warn() << "Notifier step at " << tick << std::endl;
      auto data = notifiers.begin()->second;
      notifiers.erase(notifiers.begin());
      func(data);
      return notifiers.size();
    }
    return 0;
  }
};

class Timeline {
public:
  struct Scope {
    Tick start;
    Tick end;
    bool operator<(const Scope &rhs) const { return end < rhs.end; }
    Tick len() { return end > start ? end - start : 0; }
  };

  std::map<Tick, Scope> scopes;

  Timeline() { scopes[LONG_LONG_MAX] = Scope{0, LONG_LONG_MAX}; }

  Tick transfer_time(Tick arrive, Tick delay) {
    Logger::debug() << "Timeline transfer time: " << arrive << ", delay "
                    << delay << std::endl;
    auto it = scopes.lower_bound(arrive);
    while (it->second.end - std::max(it->second.start, arrive) < delay &&
           it != scopes.end()) {
      Logger::debug() << "Skip scope " << it->second.start << "-"
                      << it->second.end << std::endl;
      it++;
    }
    ASSERT(it != scopes.end(), "Cannot find scope");
    Logger::debug() << "Use scope " << it->second.start << "-" << it->second.end
                    << std::endl;
    auto &scope = it->second;
    auto left = Scope{scope.start, std::max(scope.start, arrive)};
    auto right = Scope{std::max(scope.start, arrive) + delay, scope.end};
    auto ret = std::max(scope.start, arrive);
    scopes.erase(it);
    if (left.len() > 0) {
      Logger::debug() << "Insert new scope " << left.start << "-" << left.end
                      << std::endl;
      scopes[left.end] = left;
    }
    if (right.len() > 0) {
      Logger::debug() << "Insert new scope " << right.start << "-" << right.end
                      << std::endl;
      scopes[right.end] = right;
    }
    return ret;
  }
};
} // namespace xerxes
