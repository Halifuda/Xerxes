/**
 * This file is part of SimpleSSD.
 *
 * SimpleSSD is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SimpleSSD is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with SimpleSSD.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 */
#ifndef __PALa_ParaModule_h__
#define __PALa_ParaModule_h__

#include <list>
#include <vector>

#include "PALa_Timeline.h"
#include "pal/old/PALStatistics.h"
#include "pal/async/PALa_Statistics.h"

class Channel;
class Die;
class Plane;
typedef struct _RelatedModules {
  Channel *channel;
  Die *die;
  Plane *plane;
  CPDPBP addr;

  _RelatedModules(Channel *c, Die *d, Plane *p, CPDPBP a)
      : channel(c), die(d), plane(p), addr(a) {}
} RelatedModules;

/// @brief A basic class, used by request State Machine to
/// allocate busy timeslot.
class BaseScheduleModule {
 public:
  virtual void flushTimeslot(uint64_t tick, uint64_t *counter) = 0;
  virtual aTimeSlot allocate(const Command &cmd, const RelatedModules &m,
                             uint64_t bound, uint64_t latency) = 0;
};

/// @brief Cmd/Addr/Data Bus. Simply contains a TimeLine.
class Bus : public BaseScheduleModule {
  TimeLine timeline;

 public:
  void flushTimeslot(uint64_t tick, uint64_t *counter) override;
  aTimeSlot allocate(const Command &cmd, const RelatedModules &m,
                     uint64_t bound, uint64_t latency) override;
};

/// @brief Plane. Simply contains a TimeLine.
class Plane : public BaseScheduleModule {
  friend class Die;

  TimeLine timeline;

 public:
  TimeLine *getTimeLine() { return &timeline; }

  void flushTimeslot(uint64_t tick, uint64_t *counter) override;
  aTimeSlot allocate(const Command &cmd, const RelatedModules &m,
                     uint64_t bound, uint64_t latency) override;
  aTimeSlot getLatestBusy() { return timeline.getLatestBusySlot(); }
};

/// @brief A class to manage plane-level parallelism.
/// Manage: Multi-Plane, AMPI, conflict between Read
/// - Program/Erase and P/E suspension.
class Die : public BaseScheduleModule {
  Latency *lat;
  PALStatistics *stats;

  bool useMultiPlane;
  bool useAMPI;
  bool useSuspend;

  /** # Plane/die */
  uint32_t totalPlane;
  /** List of planes */
  std::vector<Plane> planes;
  typedef struct {
    aTimeSlot timeslot;
    PAL_OPERATION op;
    CPDPBP addr;
  } ReqInfo;
  /** Outstanding Cmd+TimeSlot on planes */
  std::vector<ReqInfo> outstanding;
  std::vector<ReqInfo> suspend_backup;

  std::function<bool()> suspendCheck;
  std::function<void(aTimeSlot)> suspendDo;

  /// @brief Check if the AMPI is satisfied. If so, return a
  /// valid timeslot; else invalid.
  aTimeSlot AMPIAllocate(const Command &cmd, const RelatedModules &m,
                         uint64_t bound, uint64_t latency);

  /// @brief Check if the Suspend is satisfied. If so, return a
  /// valid timeslot; else invalid.
  aTimeSlot SuspendAllocate(const Command &cmd, const RelatedModules &m,
                            uint64_t bound, uint64_t latency);

  /// @brief Normal allocation, not AMPI or Suspend. Panic if failed.
  aTimeSlot NormalAllocate(const Command &cmd, const RelatedModules &m,
                           uint64_t bound, uint64_t latency);

  typedef struct {
    aTimeSlot finished;
    uint64_t beforeread;
    uint64_t remained;
  } SuspendInfo;

  /// @brief Produce the new timeslots if a suspension is performed.
  /// @param oldop Old operation type, which will be suspended.
  /// @param split Split (READ arrival) tick.
  /// @param oldts Old timeslot.
  /// @param page Page offset.
  /// @return {aTimeSlot(finished slot), 
  /// beforeread(add-to-read latency), reamined(remained latency)}.
  SuspendInfo SuspendSplit(PAL_OPERATION oldop, uint64_t split, aTimeSlot oldts,
                           uint32_t page);

 public:
  /// @param p Number of plane/die
  Die(uint32_t p)
      : totalPlane(p), planes(p), outstanding(p), suspend_backup(p) {}
  ~Die() { planes.clear(); }

  uint64_t getPlaneCnt() { return totalPlane; }

  Plane *getPlane(uint32_t idx) {
    return (idx >= planes.size()) ? nullptr : &planes.at(idx);
  }

  /// @brief Set the callback used for suspension. ReqSM should set this.
  /// AYDTODO: Currently, concurrent write is not allowed, so one set of
  /// callback is tolerable. If concurrent write is enabled, check impl.
  /// @param check check if could further suspend.
  /// @param Do do the suspend.
  void setSuspendCallbacks(std::function<bool()> check,
                           std::function<void(aTimeSlot)> Do) {
    suspendCheck = check;
    suspendDo = Do;
  }

  void setParallelAccessConfig(bool MultiPlane, bool AMPI, bool Suspend) {
    useMultiPlane = MultiPlane;
    useAMPI = AMPI;
    useSuspend = Suspend;
  }

  void setLatStats(Latency *l, PALStatistics *s) {
    lat = l;
    stats = s;
  }

  void flushTimeslot(uint64_t tick, uint64_t *counter) override;

  aTimeSlot allocate(const Command &cmd, const RelatedModules &m,
                     uint64_t bound, uint64_t latency) override;

  bool planeIsFree(uint32_t p);
  PAL_OPERATION planeOp(uint32_t p);
};

class PALRequestSM;

/// @brief A class to manage die-level parallelism. Manage: SCA.
class Channel : public BaseScheduleModule {
  Latency *lat;
  PALStatistics *stats;
  ChannelStatistics chstats;

  bool useSCA;
  bool useAMPI;
  bool useSuspend;

  /** Cmd/Addr bus scheduling TimeLine.
   *  If SCA is not enabled, this bus will also be used as data bus.
   */
  Bus CmdAddrBus;
  /** Data bus scheduling TimeLine */
  Bus DataBus;
  /** Channel enable used TimeLine */
  TimeLine selfTL;

  uint32_t diePerPackage;
  /** # Die/channel */
  uint32_t totalDie;
  /** List of dies */
  std::vector<Die> dies;

  typedef std::list<PALRequestSM *> DieSubmitQ;
  std::vector<DieSubmitQ> DSQ;

  bool safeToSend(PALRequestSM *);

 public:
  /// @param dpp Number of die/package
  /// @param d Number of die/channel
  /// @param p Number of plane/die
  Channel(uint32_t dpp, uint32_t d, uint32_t p)
      : diePerPackage(dpp), totalDie(d), dies(d, Die(p)), DSQ(d) {
        chstats.numDies = d;
      }
  ~Channel() { dies.clear(); }

  /// @return A pointer to Cmd/Addr bus. If SCA is not enabled,
  /// the bus will also be used as data bus.
  Bus *getCmdAddrBus() { return &CmdAddrBus; }

  /// @return A pointer to Data bus. If SCA is enabled, use
  /// individual data bus, else use the same as cmd/addr bus.
  Bus *getDataBus() {
    if (useSCA) {
      return &DataBus;
    }
    return &CmdAddrBus;
  }

  uint64_t getDieCnt() { return totalDie; }

  Die *getDie(uint32_t idx) {
    return (idx >= dies.size()) ? nullptr : &dies.at(idx);
  }

  ChannelStatistics *getChannelStats() {
    return &chstats;
  }

  /// @brief Submit a PAL request state machine.
  /// This will help to channel scheduling, which maintain
  /// each die to have less than one PAL request per tick.
  void submit(PALRequestSM *);

  /// @brief Remove a PAL request state machine.
  /// Used by upper layer to tell channel that a request has
  /// finished. Channel then tries to send another one to a 
  /// free die.
  /// @param tick
  /// @param SMpointer
  void remove(uint64_t, PALRequestSM *);

  /// @brief Used by channel itself to schedule a SM to next stage.
  /// SM also schedule this function to Evnet Queue.
  /// @param tick
  /// @param SMpointer
  void schedNextState(uint64_t, PALRequestSM *);

  void setParallelAccessConfig(bool MultiPlane, bool AMPI, bool Suspend,
                               bool SCA) {
    useSCA = SCA;
    useAMPI = AMPI;
    useSuspend = Suspend;
    for (auto &d : dies) {
      d.setParallelAccessConfig(MultiPlane, AMPI, Suspend);
    }
  }

  void setLatStats(Latency *l, PALStatistics *s) {
    lat = l;
    stats = s;
    for (auto &d : dies) {
      d.setLatStats(l, s);
    }
  }

  void flushTimeslot(uint64_t tick, uint64_t *counter) override;

  aTimeSlot allocate(const Command &cmd, const RelatedModules &m,
                     uint64_t bound, uint64_t latency) override;
};

#endif  // __PALa_ParaModule_h__