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
#ifndef __PALa_ReqSM_h__
#define __PALa_ReqSM_h__

#include <functional>
#include <utility>

#include "PALa_ParaModule.h"
#include "error/retry.hh"

class BaseState {
 public:
  typedef std::pair<BaseState *, BaseScheduleModule *> NextStateInfo;
  virtual NextStateInfo nextState(const Command &, RelatedModules &) = 0;
  virtual uint64_t finishingLatency(const Command &, Latency *, CPDPBP &) = 0;
  virtual void updateChannelStats(const Command &, Latency *, OperStat *,
                                  uint64_t wait, uint64_t lat) = 0;
  virtual BaseState *trans(const Command &, RelatedModules &,
                           BaseScheduleModule **);
};

/// @brief Request State Machine.
class PALRequestSM {
  private:
    uint64_t id;
    Latency *lat;
    ChannelStatistics *chstats;
    RelatedModules relatedModules;
    std::function<void(uint64_t, uint64_t)> commitCallback;

    Command cmd;
    SimpleSSD::Event curEvent;
    BaseScheduleModule *curSched;
    aTimeSlot curTimeSlot;
    BaseState *curState;

    uint8_t suspendTime;
    uint8_t maxSuspend;

    bool checkSuspend();
    void doSuspend(aTimeSlot newslot);

 public:
  PALRequestSM(uint64_t i, Latency *l, ChannelStatistics *cs,
               const Command &cmd_, Channel *c, Die *d, Plane *p, CPDPBP addr_,
               uint64_t maxsuspend, SimpleSSD::ERROR::Retry *retry)
      : id(i),
        lat(l),
        chstats(cs),
        relatedModules(c, d, p, addr_),
        cmd(cmd_),
        curEvent(0),
        curSched(nullptr),
        curTimeSlot(aTimeSlot()),
        curState(nullptr),
        suspendTime(0),
        maxSuspend(maxsuspend), 
        retry(retry) {retry_num = 0;}

  PALRequestSM(const PALRequestSM &sm)
      : id(sm.id),
        lat(sm.lat),
        chstats(sm.chstats),
        relatedModules(sm.relatedModules),
        commitCallback(sm.commitCallback),
        cmd(sm.cmd),
        curEvent(sm.curEvent),
        curSched(sm.curSched),
        curTimeSlot(sm.curTimeSlot),
        curState(sm.curState),
        suspendTime(sm.suspendTime),
        maxSuspend(sm.maxSuspend) {retry_num = 0;}

  void next(uint64_t);
  void setCommitCallBack(std::function<void(uint64_t, uint64_t)> f) {
    commitCallback = f;
  }
  void start();

  uint64_t getID() { return id; }
  CPDPBP getAddr() { return relatedModules.addr; }
  PAL_OPERATION getOp() { return cmd.operation; }
  aTimeSlot getTimeslot() { return curTimeSlot; }
  bool atFlashState();
  bool ending();

  SimpleSSD::ERROR::Retry *retry;
  uint32_t retry_num;
  Tick arriveTick;
  Tick start_;
};

/** ---------- States ---------- */
namespace RequestStates {
class ChannelEnable : public BaseState {
 public:
  NextStateInfo nextState(const Command &, RelatedModules &) override;
  uint64_t finishingLatency(const Command &, Latency *, CPDPBP &) override;
  void updateChannelStats(const Command &, Latency *, OperStat *, uint64_t wait,
                          uint64_t lat) override;
};

class CmdAddrIn : public BaseState {
 public:
  NextStateInfo nextState(const Command &, RelatedModules &) override;
  uint64_t finishingLatency(const Command &, Latency *, CPDPBP &) override;
  void updateChannelStats(const Command &, Latency *, OperStat *, uint64_t wait,
                          uint64_t lat) override;
};

class DataIn : public BaseState {
 public:
  NextStateInfo nextState(const Command &, RelatedModules &) override;
  uint64_t finishingLatency(const Command &, Latency *, CPDPBP &) override;
  void updateChannelStats(const Command &, Latency *, OperStat *, uint64_t wait,
                          uint64_t lat) override;
};

class FlashRead : public BaseState {
 public:
  NextStateInfo nextState(const Command &, RelatedModules &) override;
  uint64_t finishingLatency(const Command &, Latency *, CPDPBP &) override;
  void updateChannelStats(const Command &, Latency *, OperStat *, uint64_t wait,
                          uint64_t lat) override;
};

class FlashProgram : public BaseState {
 public:
  NextStateInfo nextState(const Command &, RelatedModules &) override;
  uint64_t finishingLatency(const Command &, Latency *, CPDPBP &) override;
  void updateChannelStats(const Command &, Latency *, OperStat *, uint64_t wait,
                          uint64_t lat) override;
};

class FlashErase : public BaseState {
 public:
  NextStateInfo nextState(const Command &, RelatedModules &) override;
  uint64_t finishingLatency(const Command &, Latency *, CPDPBP &) override;
  void updateChannelStats(const Command &, Latency *, OperStat *, uint64_t wait,
                          uint64_t lat) override;
};

class DataOut : public BaseState {
 public:
  NextStateInfo nextState(const Command &, RelatedModules &) override;
  uint64_t finishingLatency(const Command &, Latency *, CPDPBP &) override;
  void updateChannelStats(const Command &, Latency *, OperStat *, uint64_t wait,
                          uint64_t lat) override;
};

class ChannelDMA0 : public BaseState {
 public:
  NextStateInfo nextState(const Command &, RelatedModules &) override;
  uint64_t finishingLatency(const Command &, Latency *, CPDPBP &) override;
  void updateChannelStats(const Command &, Latency *, OperStat *, uint64_t wait,
                          uint64_t lat) override;
};
class ChannelDMA1 : public BaseState {
 public:
  NextStateInfo nextState(const Command &, RelatedModules &) override;
  uint64_t finishingLatency(const Command &, Latency *, CPDPBP &) override;
  void updateChannelStats(const Command &, Latency *, OperStat *, uint64_t wait,
                          uint64_t lat) override;
};

class Ending : public BaseState {
 public:
  NextStateInfo nextState(const Command &, RelatedModules &) override {
    return make_pair(nullptr, nullptr);
  }
  uint64_t finishingLatency(const Command &, Latency *, CPDPBP &) override {
    return 0;
  }
  void updateChannelStats(const Command &, Latency *, OperStat *, 
                          uint64_t, uint64_t) override {}
};
}  // namespace RequestStates

#endif  // __PALa_ReqSM_h__