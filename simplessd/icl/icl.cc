/*
 * Copyright (C) 2017 CAMELab
 *
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
 */

#include "icl/icl.hh"

#include "count/reqcount.hh"
#include "dram/simple.hh"
#include "icl/generic_cache.hh"
#include "util/algorithm.hh"
#include "util/def.hh"

namespace SimpleSSD {

namespace ICL {

ICL::ICL(ConfigReader &c) : conf(c) {
  switch (conf.readInt(CONFIG_DRAM, DRAM::DRAM_MODEL)) {
    case DRAM::SIMPLE_MODEL:
      pDRAM = new DRAM::SimpleDRAM(conf);

      break;
    default:
      panic("Undefined DRAM model");

      break;
  }

  pFTL = new FTL::FTL(conf, pDRAM);

  FTL::Parameter *param = pFTL->getInfo();

  if (conf.readBoolean(CONFIG_FTL, FTL::FTL_USE_RANDOM_IO_TWEAK)) {
    totalLogicalPages =
        param->totalLogicalBlocks * param->pagesInBlock * param->ioUnitInPage;
    logicalPageSize = param->pageSize / param->ioUnitInPage;
  }
  else {
    totalLogicalPages = param->totalLogicalBlocks * param->pagesInBlock;
    logicalPageSize = param->pageSize;
  }

  pCache = new GenericCache(conf, pFTL, pDRAM);
  std::function<void()> f = [this]() { Commit(); };
  pCache->setCommitCallback(f);
}

ICL::~ICL() {
  delete pCache;
  delete pFTL;
  delete pDRAM;
}

void ICL::updateSubmit() {
  DMAFunction doRequest = [this](uint64_t beginAt, void *context) {
    auto pReq = (Request *)context;
    Request reqInternal = *pReq;
    reqInternal.reqID = pReq->reqID;
    reqInternal.offset = pReq->offset;
    uint64_t reqRemain = pReq->length;
    uint64_t nlp = (pReq->offset + pReq->length - 1) / logicalPageSize + 1;
    uint64_t tick = beginAt, finishedAt = beginAt;

    auto iter = submitQueue.begin();
    for (; iter != submitQueue.end(); ++iter) {
      if (iter->first.reqID == pReq->reqID) {
        break;
      }
    }

    debugprint(LOG_ICL,
               "TYPE %u  | REQ %7u | LCA %" PRIu64 " + %" PRIu64
               " | BYTE %" PRIu64 ", %" PRIu64,
               pReq->reqType, pReq->reqID, pReq->range.slpn, pReq->range.nlp,
               pReq->offset, pReq->length);

    if (iter == submitQueue.end()) {
      panic("ICL request ID %lu dosen't appear in SQ", pReq->reqID);
    }

    switch (pReq->reqType) {
      case READ:
        iter->first.readValidPage = false;
        reqInternal.reqSubID = 0;
        reqInternal.readValidPage = false;
        for (uint64_t i = 0; i < nlp; i++) {
          tick = beginAt;
          reqInternal.range.slpn = pReq->range.slpn + i;
          reqInternal.length =
              MIN(reqRemain, logicalPageSize - reqInternal.offset);
          pCache->read(reqInternal, tick);
          reqRemain -= reqInternal.length;
          reqInternal.offset = 0;
        }
        iter->second = reqInternal.reqSubID;
        break;

      case WRITE:
        reqInternal.reqSubID = 0;
        for (uint64_t i = 0; i < nlp; i++) {
          tick = beginAt;
          reqInternal.internal = false;
          reqInternal.range.slpn = pReq->range.slpn + i;
          reqInternal.length =
              MIN(reqRemain, logicalPageSize - reqInternal.offset);
          pCache->write(reqInternal, tick);
          reqRemain -= reqInternal.length;
          reqInternal.offset = 0;
        }
        iter->second = reqInternal.reqSubID;
        break;

      case FLUSH:
        pCache->flush(reqInternal, tick);
        finishedAt = MAX(finishedAt, tick);
        break;

      case TRIM:
        pCache->trim(reqInternal, tick);
        finishedAt = MAX(finishedAt, tick);
        break;

      case FORMAT:
        pCache->format(reqInternal, tick);
        finishedAt = MAX(finishedAt, tick);
        break;
      default:
        panic("Invalid ICL request type, tick %lu, queue size %lu+1, ID "
              "%lu, "
              "Type %u, Range %lu->%lu",
              tick, submitQueue.size(), pReq->reqID, pReq->reqType,
              pReq->offset, pReq->range.nlp);
        break;
    }

    pCache->updateSubmit(true);
    delete pReq;
  };
  for (auto iter = submitQueue.begin(); iter != submitQueue.end(); ++iter) {
    // Send events in SQ that haven't been sent
    if (iter->second >= 0) {
      continue;
    }
    CPU::FUNCTION func = CPU::READ;
    uint64_t nlp =
        (iter->first.offset + iter->first.length - 1) / logicalPageSize + 1;
    // printf("ICL: req size %lu\n", nlp);
    switch (iter->first.reqType) {
      case READ:
        iter->second = nlp;
        func = CPU::READ;
        break;
      case WRITE:
        iter->second = nlp;
        func = CPU::WRITE;
        break;
      case TRIM:
      case FLUSH:
      case FORMAT:
        iter->second = 1;
        func = CPU::FLUSH;
        break;
      default:
        panic("Invalid ICL request type %u", iter->first.reqType);
        break;
    }
    // doRequest(getTick(), new Request(iter->first));
    // iter->first.finishAt += applyLatency(CPU::ICL, func);
    execute(CPU::ICL, func, doRequest, new Request(iter->first));
  }
}

void ICL::Commit() {
  updateSubmit();
  Request subreq;
  while (pCache->commitToUpper(subreq)) {
    for (auto iter = submitQueue.begin(); iter != submitQueue.end(); ++iter) {
      // we only check reqID, not reqsubID, for the latter
      // will not be set when the Request is submitted.
      if (iter->first.reqID == subreq.reqID) {
        iter->second--;
        if(subreq.readValidPage){
          iter->first.readValidPage = true;
        }
        iter->first.finishAt = MAX(iter->first.finishAt, subreq.finishAt);
        debugprint(LOG_ICL,
                   "Inner Cache commit ID=%lu-%lu, @%lu-%lu (%lu), remain %lu",
                   subreq.reqID, subreq.reqSubID, iter->first.beginAt,
                   iter->first.finishAt,
                   iter->first.finishAt - iter->first.beginAt, iter->second);
      }
    }
  }
  // move finished events in SQ to CQ.
  updateCommit();
  // callback.
  commitCallback();
}

/// @brief Yuda: At ICL, requests are split into sub-requests,
/// a counter is recorded in each entry of submit queue, when a
/// sub-request is commited, the counter will be decreased by 1.
/// After the last sub-request is commited, i.e., the counter is
/// decreased to 0, the parent request can be commit.
void ICL::updateCommit() {
  // remove all the comleted requests
  bool find = false;
  do {
    find = false;
    auto iter = submitQueue.begin();
    while (iter != submitQueue.end()) {
      // Yuda: must equal to 0,
      // -1 has been used to denote un-sent event
      if (iter->second == 0) {
        find = true;
        break;
      }
      ++iter;
    }
    if (find) {
      Request newreq = iter->first;
      commitQueue.push_back(newreq);
      submitQueue.erase(iter);
    }
  } while (find);
}

/// @brief Yuda: Commit one request to HIL.
/// @param req Store request. HIL passes to this method.
/// @return true if exists, otherwise false.
bool ICL::commitToUpper(Request &req) {
  if (commitQueue.empty()) {
    return false;
  }
  req = commitQueue.front();
  commitQueue.pop_front();
  return true;
}

void ICL::read(Request &req, uint64_t & /* tick */) {
  Count::CheckReadSeq(req.range.slpn * logicalPageSize + req.offset,
                      req.length);
  Request newreq = req;
  submitQueue.push_back(make_pair(newreq, -1));
}

void ICL::write(Request &req, uint64_t & /* tick */) {
  Count::CheckWriteSeq(req.range.slpn * logicalPageSize + req.offset,
                       req.length);
  Request newreq = req;
  submitQueue.push_back(make_pair(newreq, -1));
}

void ICL::flush(Request &req, uint64_t & /* tick */) {
  Request newreq = req;
  submitQueue.push_back(make_pair(newreq, -1));
}

void ICL::trim(Request &req, uint64_t & /* tick */) {
  Request newreq = req;
  submitQueue.push_back(make_pair(newreq, -1));
}

void ICL::format(Request &req, uint64_t & /* tick */) {
  Request newreq = req;
  submitQueue.push_back(make_pair(newreq, -1));
}

void ICL::getLPNInfo(uint64_t &t, uint32_t &s) {
  t = totalLogicalPages;
  s = logicalPageSize;
}

uint64_t ICL::getUsedPageCount(uint64_t lcaBegin, uint64_t lcaEnd) {
  uint32_t ratio = pFTL->getInfo()->ioUnitInPage;

  return pFTL->getUsedPageCount(lcaBegin / ratio, lcaEnd / ratio) * ratio;
}

void ICL::getStatList(std::vector<Stats> &list, std::string prefix) {
  pCache->getStatList(list, prefix + "icl.");
  pDRAM->getStatList(list, prefix + "dram.");
  pFTL->getStatList(list, prefix);
}

void ICL::getStatValues(std::vector<double> &values) {
  pCache->getStatValues(values);
  pDRAM->getStatValues(values);
  pFTL->getStatValues(values);
}

void ICL::resetStatValues() {
  pCache->resetStatValues();
  pDRAM->resetStatValues();
  pFTL->resetStatValues();
}

}  // namespace ICL

}  // namespace SimpleSSD
