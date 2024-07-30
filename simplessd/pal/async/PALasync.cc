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

#include "PALasync.h"

#include "util/algorithm.hh"

PALasync::PALasync(PALStatistics *s, SimpleSSD::PAL::Parameter *p,
                   SimpleSSD::ConfigReader *pC, Latency *l)
    : pParam(p),
      lat(l),
      stats(s),
      totalDie(p->channel * p->package * p->die),
      curReqID(1),
      channels(p->channel, Channel(p->die, p->package * p->die, p->plane)) {
  useMultiPlane = pC->readBoolean(SimpleSSD::CONFIG_PAL,
                                  SimpleSSD::PAL::NAND_USE_MULTI_PLANE_OP);
  useAMPI =
      pC->readBoolean(SimpleSSD::CONFIG_PAL, SimpleSSD::PAL::NAND_USE_AMPI);
  useSuspend =
      pC->readBoolean(SimpleSSD::CONFIG_PAL, SimpleSSD::PAL::NAND_USE_SUSPEND);
  useSCA = pC->readBoolean(SimpleSSD::CONFIG_PAL, SimpleSSD::PAL::NAND_USE_SCA);

  SimpleSSD::debugprint(SimpleSSD::LOG_PAL_ASYNC, "AMPI %s",
                        useAMPI ? "enabled" : "disabled");
  SimpleSSD::debugprint(SimpleSSD::LOG_PAL_ASYNC, "P/E Suspension %s",
                        useSuspend ? "enabled" : "disabled");
  SimpleSSD::debugprint(SimpleSSD::LOG_PAL_ASYNC, "SCA %s",
                        useSCA ? "enabled" : "disabled");
  if (lat->useNewModel()) {
    maxPageWriteSuspend = pC->readUint(SimpleSSD::CONFIG_PAL,
                                       SimpleSSD::PAL::MAX_PAGE_WRITE_SUSPEND);
    maxBlockEraseSuspend = pC->readUint(
        SimpleSSD::CONFIG_PAL, SimpleSSD::PAL::MAX_BLOCK_ERASE_SUSPEND);
  } else {
    maxPageWriteSuspend = 4;
    maxBlockEraseSuspend = 4;
  }
  SimpleSSD::debugprint(SimpleSSD::LOG_PAL_ASYNC, "Page max write suspend time: %lu",
                        maxPageWriteSuspend);
  SimpleSSD::debugprint(SimpleSSD::LOG_PAL_ASYNC, "Block max erase suspend time: %lu",
                        maxBlockEraseSuspend);
  for (auto &ch : channels) {
    ch.setParallelAccessConfig(useMultiPlane, useAMPI, useSuspend, useSCA);
    ch.setLatStats(lat, stats);
  }
}

PALasync::~PALasync() {}

void PALasync::submit(Command &cmd, CPDPBP &addr, SimpleSSD::PAL::Request *pReq,
                      uint64_t *pCounter, SimpleSSD::ERROR::Retry *retry) {
 
       
    Channel *ch = &channels[addr.Channel];
    Die *die = ch->getDie(addr.Package * pParam->die + addr.Die);
    Plane *plane = die->getPlane(addr.Plane);
    ChannelStatistics *chstats = ch->getChannelStats();

    uint64_t maxsuspend = 0;
    if (cmd.operation == OPER_WRITE) maxsuspend = maxPageWriteSuspend;
    if (cmd.operation == OPER_ERASE) maxsuspend = maxBlockEraseSuspend;

    SimpleSSD::debugprint(
        SimpleSSD::LOG_PAL_ASYNC,
        "submit ID=%lu-%lu-%lu block%u-page%u, inner id is %lu",
        pReq->reqID, pReq->reqSubID, pReq->ftlSubID, pReq->blockIndex,
        pReq->pageIndex, curReqID);

    PALRequestSM *req = new PALRequestSM(curReqID++, lat, chstats, cmd, ch, die,
                                  plane, addr, maxsuspend, retry);
    req->setCommitCallBack(
        [this](uint64_t tick, uint64_t id) { commitReq(tick, id); });
    requests.push_back(make_pair(req, make_pair(pReq, pCounter)));
    ch->submit(req);
}

void PALasync::commitReq(uint64_t tick, uint64_t id) {
  auto iter = requests.begin();
  bool find = false;
  PALRequestSM *pSM = nullptr;
  SimpleSSD::PAL::Request *pReq = nullptr;
  uint64_t *pCounter = nullptr;
  for (; iter != requests.end(); ++iter) {
    if (iter->first->getID() == id) {
      find = true;
      pSM = iter->first;
      pReq = iter->second.first;
      pCounter = iter->second.second;
      requests.erase(iter);
      break;
    }
  }
  if (!find) {
    SimpleSSD::panic("Unexpected PALasync request SM id=%lu", id);
  }
  pReq->finishAt = tick;
  if (*pCounter > 0) *pCounter = (*pCounter) - 1;

  auto addr = pSM->getAddr();
  SimpleSSD::debugprint(SimpleSSD::LOG_PAL_ASYNC,
                        "commit %lu, ID=%lu-%lu-%lu block%u-page%u",
                        pSM->getID(), pReq->reqID, pReq->reqSubID,
                        pReq->ftlSubID, pReq->blockIndex, pReq->pageIndex);

  // Clean timeslot first, because we need to maintain newest die info.
  for (auto &ch : channels) {
    ch.flushTimeslot(tick, nullptr);
  }
  channels[addr.Channel].remove(tick, pSM);
  delete pSM;

  commitCallback();
}

void PALasync::getStatList(std::vector<SimpleSSD::Stats> &list,
                           std::string prefix) {
  for (uint32_t i = 0; i < pParam->channel; ++i) {
    std::string newprefix = prefix + "channel" + std::to_string(i) + ".";
    channels[i].getChannelStats()->getStatList(list, newprefix);
  }
}
void PALasync::getStatValues(std::vector<double> &values) {
  for (auto &ch : channels) {
    ch.getChannelStats()->getStatValues(values);
  }
}
void PALasync::resetStatValues() {
  for (auto &ch : channels) {
    ch.getChannelStats()->reset();
  }
}