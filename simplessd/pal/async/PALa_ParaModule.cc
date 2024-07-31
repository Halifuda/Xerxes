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
#include <exception>
#include <map>
#include <utility>

#include "PALa_ParaModule.h"

#include "PALa_ReqSM.h"
#include "sim/trace.hh"
#include "util/algorithm.hh"

static std::unordered_map<PALRequestSM *, int> livingSM;

//----- Bus & Plane -----

aTimeSlot Bus::allocate(const Command &, const RelatedModules &, uint64_t bound,
                        uint64_t latency) {
  return timeline.allocateTimeSlot(bound, latency);
}

aTimeSlot Plane::allocate(const Command &, const RelatedModules &,
                          uint64_t bound, uint64_t latency) {
  return timeline.allocateTimeSlot(bound, latency);
}

// ----- Die -----

aTimeSlot Die::AMPIAllocate(const Command &cmd, const RelatedModules &m,
                            uint64_t bound, uint64_t latency) {
  aTimeSlot invalid = aTimeSlot(1, 0);
  if (!useAMPI || cmd.operation != OPER_READ) {
    // printf("none read AMPI check, b%d op%d\n", useAMPI, cmd.operation);
    return invalid;
  }
  bool is_outstanding = false;
  for (uint32_t i = 0; i < totalPlane; ++i) {
    auto &p = outstanding[i];
    if (i == m.addr.Plane) {
      // skip same plane
      continue;
    }
    if (p.timeslot) {
      is_outstanding = true;
      if (p.op != OPER_READ) {
        // printf("@%lu AMPI outstanding fail, [%lu,%lu], %d\n",
        //        SimpleSSD::getTick(), p.first.start, p.first.end,
        //        p.second.operation);
        return invalid;
      }
    }
  }
  if (!is_outstanding) {
    return invalid;
  }
  // AMPI satisfied
  SimpleSSD::debugprint(SimpleSSD::LOG_PAL_ASYNC, "AMPI triggered");
  auto &p = planes[m.addr.Plane];
  auto t = p.getLatestBusy();
  uint64_t new_bound = MAX(t.end, bound);
  return p.allocate(cmd, m, new_bound, latency);
}

Die::SuspendInfo Die::SuspendSplit(PAL_OPERATION oldop, uint64_t split,
                                   aTimeSlot oldts, uint32_t page) {
  SuspendInfo ret{aTimeSlot(), 0, 0};
  auto ts_2 = oldts;
  auto ts_1 = ts_2.split(split);

  // If new latency is not applied, just split.
  if (!lat->useNewModel()) {
    ret.finished = ts_1;
    ret.beforeread = 0;
    ret.remained = ts_2.len();
    return ret;
  }

  if (oldop == OPER_ERASE) {
    uint64_t ephase = lat->NewModelLatency(0, OPER_ERASE, NEWLAT_ERASE_PHASE);
    uint64_t vphase = lat->NewModelLatency(0, OPER_ERASE, NEWLAT_ERASE_VERIFY);
    uint64_t reset = lat->NewModelLatency(0, OPER_ERASE, NEWLAT_ERASE_VRESET);

    // Only last reset phase.
    if (oldts.len() <= reset) {
      ret.finished = oldts;
      ret.beforeread = 0;
      ret.remained = 0;
    }
    // At verify immediate suspension
    else if (oldts.len() <= vphase + reset) {
      ret.finished = ts_1;
      ret.beforeread = reset;
      // verify should be redone
      ret.remained = vphase + reset;
    }
    // At first reset phase.
    else if (oldts.len() <= reset + vphase + reset) {
      // finish the reset
      ret.finished = {ts_1.start, ts_1.start + ephase + reset};
      ret.beforeread = reset;
      ret.remained = vphase + reset;
    }
    // At erase immediate suspension
    else {
      ret.finished = ts_1;
      ret.beforeread = reset;
      // erase resume has no overhead
      ret.remained = ts_2.len();
    }

  } else if (oldop == OPER_WRITE) {
    uint64_t pphase =
        lat->NewModelLatency(page, OPER_WRITE, NEWLAT_ISPP_PROGRAM);
    uint64_t vphase =
        lat->NewModelLatency(page, OPER_WRITE, NEWLAT_ISPP_VERIFY);
    uint64_t lb = lat->NewModelLatency(page, OPER_WRITE, NEWLAT_LOAD_BUFFER);
    uint64_t iterlat = pphase + vphase;

    uint64_t remained_iters = ts_2.len() / iterlat;
    uint64_t remained_lats = remained_iters * iterlat;
    uint64_t tl = ts_2.len() - remained_lats;

    // Whatever, IPS will not introduce overhead to read, and 
    // must and only introduce load buffer to remained write
    ret.beforeread = 0;
    ret.remained = remained_lats + lb;

    // conditions:
    // [p][v]...[p][v][!][p][v]...[p][v]
    // ----- ts_1 -----! --- remained --
    if (tl == 0) {
      ret.finished = ts_1;
    }
    // [p][v]...[p][v[!]][p][v]...[p][v]
    // ----- ts_1 ----!tl--- remained --
    else if (tl < vphase) {
      ret.finished = {ts_1.start, ts_1.end + tl};
    }
    // [p][v]...[p][!][v][p][v]...[p][v]
    // --- ts_1 ----![tl]--- remained --
    else if (tl == vphase) {
      ret.finished = ts_1;
      // plus a verify phase
      ret.remained += tl;
    }
    // [p][v]...[p[!]][v][p][v]...[p][v]
    // --- ts_1 ---！[tl]--- remained --
    else if (tl < vphase + pphase) {
      ret.finished = {ts_1.start, ts_1.end + tl - vphase};
      // plus a verify phase
      ret.remained += vphase;
    }
    // should not reach here because equal to condition 1
    // avoid potiential bugs
    else {
      ret.finished = ts_1;
    }
  } else {
    // not WRITE or ERASE
  }
  return ret;
}

aTimeSlot Die::SuspendAllocate(const Command &cmd, const RelatedModules &m,
                               uint64_t bound, uint64_t latency) {
  aTimeSlot invalid = aTimeSlot(1, 0);
  if (!useSuspend || cmd.operation != OPER_READ) {
    return invalid;
  }
  bool suspend = false;
  uint32_t i = 0;
  for (; i < totalPlane; ++i) {
    auto &p = outstanding[i];
    if (p.timeslot) {
      if (p.op == OPER_WRITE || p.op == OPER_ERASE) {
        if (suspendCheck()) {
          suspend = true;
          break;
        }
      }
    }
  }
  if (!suspend) {
    return invalid;
  }
  // Do suspend
  auto &p = planes[m.addr.Plane];
  // backup suspend req
  suspend_backup[i] = outstanding[i];
  // realloc
  auto &w = planes[i];
  auto write_ts = w.timeline.getLatestBusySlot();
  if (write_ts.end <= bound) {
    // filter the unexpected suspend
    return invalid;
  }
  if (w.timeline.removeBusySlot(write_ts.start, write_ts.end)) {
    auto info = SuspendSplit(suspend_backup[i].op, bound, write_ts,
                             suspend_backup[i].addr.Page);
    if (info.finished) {
      if (info.finished.len() > 0) {
        // <cmd> and <m> is not used in Plane::allocate()
        info.finished =
            w.allocate(cmd, m, info.finished.start, info.finished.len());
        if (!info.finished) {
          goto SUSPEND_ALLOC_FAIL;
        }
      }

      // <cmd> and <m> is not used in Plane::allocate()
      auto ret_ts =
          p.allocate(cmd, m, info.finished.end, latency + info.beforeread);
      if (!ret_ts) {
        w.timeline.removeBusySlot(info.finished.start, info.finished.end);
        goto SUSPEND_ALLOC_FAIL;
      }

      // default, remained is finished, as there is no remained phase
      auto remained = info.finished;
      // if there is, allocate
      if (info.remained > 0) {
        // <cmd> and <m> is not used in Plane::allocate()
        remained = w.allocate(cmd, m, ret_ts.end, info.remained);
        if (!remained) {
          p.timeline.removeBusySlot(ret_ts.start, ret_ts.end);
          w.timeline.removeBusySlot(info.finished.start, info.finished.end);
          goto SUSPEND_ALLOC_FAIL;
        }
      }
      if (remained) {
        suspendDo(remained);
      }
      auto outputremain = remained;
      if (info.remained == 0) {
        outputremain.start = outputremain.end;
      }
      SimpleSSD::debugprint(
          SimpleSSD::LOG_PAL_ASYNC,
          "Suspend %lu-%lu: finished %lu-%lu, read %lu-%lu, remained %lu-%lu",
          write_ts.start, write_ts.end, info.finished.start, info.finished.end,
          ret_ts.start, ret_ts.end, outputremain.start, outputremain.end);
      return ret_ts;
    }/*
    else if (bound < write_ts.start) {
      auto ret_ts = p.allocate(cmd, m, bound, latency);
      if (!ret_ts) {
        goto SUSPEND_ALLOC_FAIL;
      }
      auto remained = w.allocate(cmd, m, ret_ts.end, write_ts.len());
      if (!remained) {
        p.timeline.removeBusySlot(ret_ts.start, ret_ts.end);
        goto SUSPEND_ALLOC_FAIL;
      }
      suspend_backup[i].timeslot = remained;
      suspendDo(remained);
      SimpleSSD::debugprint(
        SimpleSSD::LOG_PAL_ASYNC,
        "Suspend %lu-%lu: read %lu-%lu, forward %lu-%lu",
        write_ts.start, write_ts.end,
        ret_ts.start, ret_ts.end, remained.start, remained.end
      );
    }*/
  }
SUSPEND_ALLOC_FAIL:
  // Failed whatever, realloate the old one.
  suspend_backup[i].timeslot = aTimeSlot(1, 0);
  w.timeline.allocateTimeSlot(write_ts.start, write_ts.len());
  return invalid;
}

aTimeSlot Die::NormalAllocate(const Command &cmd, const RelatedModules &m,
                              uint64_t bound, uint64_t latency) {
  aTimeSlot latest;
  for (auto &p : outstanding) {
    if (p.timeslot.end > latest.end) latest = p.timeslot;
  }
  uint64_t new_bound = MAX(bound, latest.end);
  auto &p = planes[m.addr.Plane];
  auto ret = p.allocate(cmd, m, new_bound, latency);
  return ret;
}

aTimeSlot Die::allocate(const Command &cmd, const RelatedModules &m,
                        uint64_t bound, uint64_t latency) {
  aTimeSlot allocated = aTimeSlot(1, 0);
  if (cmd.operation == OPER_READ) {
    allocated = AMPIAllocate(cmd, m, bound, latency);
    if (!allocated) allocated = SuspendAllocate(cmd, m, bound, latency);
  }
  if (!allocated) allocated = NormalAllocate(cmd, m, bound, latency);
  if (!allocated) SimpleSSD::panic("Die: falied to allocated timeslot");
  outstanding[m.addr.Plane] = {allocated, cmd.operation, m.addr};
  // printf("@%lu CPDP: %u-%u-%u-%u allocate type%d %lu-%lu\n", bound,
  //        m.addr.Channel, m.addr.Package, m.addr.Die, m.addr.Plane,
  //        cmd.operation, allocated.start, allocated.end);
  return allocated;
}

bool Die::planeIsFree(uint32_t p) {
  return !outstanding[p].timeslot && !suspend_backup[p].timeslot;
}

PAL_OPERATION Die::planeOp(uint32_t p) {
  if (planeIsFree(p)) {
    return OPER_NUM;
  }
  if (!outstanding[p].timeslot) {
    return suspend_backup[p].op;
  }
  return outstanding[p].op;
}

// ----- Channel -----

aTimeSlot Channel::allocate(const Command &, const RelatedModules &,
                            uint64_t bound, uint64_t latency) {
  return selfTL.allocateTimeSlot(bound, latency);
}

bool Channel::safeToSend(PALRequestSM *pSM) {
  if (livingSM.count(pSM) == 0) {
    return false;
  }
  // Only care about die (flash)
  if (!pSM->atFlashState()) {
    return true;
  }
  auto addr = pSM->getAddr();
  uint32_t d = addr.Package * diePerPackage + addr.Die;
  uint32_t p = addr.Plane;
  if (dies[d].planeIsFree(p)) {
    return true;
  }
  // Do not block current req
  if (!DSQ[d].empty() && DSQ[d].front() == pSM) {
    return true;
  }
  // else check suspend
  if (useSuspend && pSM->getOp() == OPER_READ &&
      (dies[d].planeOp(p) == OPER_WRITE || dies[d].planeOp(p) == OPER_ERASE)) {
    return true;
  }
  return false;
}

void Channel::submit(PALRequestSM *pSM) {
  auto addr = pSM->getAddr();
  uint32_t d = addr.Package * diePerPackage + addr.Die;
  DSQ[d].push_back(pSM);
  livingSM.insert({pSM, 1});
  pSM->start();
}

void Channel::remove(uint64_t tick, PALRequestSM *pSM) {
  auto addr = pSM->getAddr();
  uint32_t d = addr.Package * diePerPackage + addr.Die;
  DSQ[d].remove(pSM);
  livingSM.erase(pSM);
  if (!DSQ[d].empty()) {
    auto next = DSQ[d].front();
    if (next->getTimeslot().end < tick && (!next->ending())) {
      schedNextState(tick, next);
    }
  }
}

void Channel::schedNextState(uint64_t tick, PALRequestSM *pSM) {
  if (livingSM.count(pSM) == 0) {
    return;
  }
  auto addr = pSM->getAddr();
  uint32_t d = addr.Package * diePerPackage + addr.Die;
  if (d >= totalDie) {
    return;
  }
  if (safeToSend(pSM)) {
    pSM->next(tick);
  }
  for (auto p : DSQ[d]) {
    if (p != pSM && p->getTimeslot().end < tick && 
        (!p->ending()) && safeToSend(p)) {
          p->next(tick);
        }
  }
  
}

// ----- Flush timeslot -----

void Bus::flushTimeslot(uint64_t tick, uint64_t *counter) { 
  timeline.flushTimeslot(tick, counter); 
}

void Plane::flushTimeslot(uint64_t tick, uint64_t *counter) { 
  timeline.flushTimeslot(tick, counter); 
}

void Die::flushTimeslot(uint64_t tick, uint64_t *counter) {
  for (auto &p : planes) {
    p.flushTimeslot(tick, counter);
  }

  // First, flush all outstanding
  for (uint32_t i = 0; i < totalPlane; ++i) {
    auto &p = outstanding[i];
    if (p.timeslot.end <= tick) {
      // false timeslot
      p.timeslot = aTimeSlot(1, 0);
    }
  }
  // Second, test if still outstanding
  bool still = false;
  for (auto &p : outstanding) {
    if (p.timeslot) {
      still = true;
      break;
    }
  }
  // If there is not, restore backup
  if (!still) {
    for (uint32_t i = 0; i < totalPlane; ++i) {
      if (suspend_backup[i].timeslot > tick) {
        outstanding[i] = suspend_backup[i];
        suspend_backup[i].timeslot = aTimeSlot(1, 0);
        // Avoid multiple restore
        break;
      } else {
        suspend_backup[i].timeslot = aTimeSlot(1, 0);
      }
    }
  }
}

void Channel::flushTimeslot(uint64_t tick, uint64_t *counter = nullptr) {
  selfTL.flushTimeslot(tick, counter);
  CmdAddrBus.flushTimeslot(tick, chstats.getCABusyCounter());
  DataBus.flushTimeslot(tick, chstats.getDataBusyCounter());
  for (auto &d : dies) {
    d.flushTimeslot(tick, chstats.getDieBusyCounter());
  }
}