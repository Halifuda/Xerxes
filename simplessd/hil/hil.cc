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

#include "hil/hil.hh"
#include "hil/nvme/namespace.hh"
#include "util/algorithm.hh"

namespace SimpleSSD {

namespace HIL {

HIL::HIL(ConfigReader &c) : conf(c), reqCount(0), lastScheduled(0) {
  pICL = new ICL::ICL(conf);
 
  // set callback for ICL
  std::function<void()> f = [this]() { Commit(); };
  pICL->setCommitCallback(f);

  memset(&stat, 0, sizeof(stat));

  debugprint(LOG_HIL, "HIL-fin,id,type,begin@,finish@,latency");

  memset(&stat, 0, sizeof(stat));

  completionEvent = allocate([this](uint64_t) { completion(); });
}

HIL::~HIL() {
  delete pICL;
}

/// @brief Yuda: warning: this method should be called after command submission,
/// currently, this is called right after read/write/trim/flush/format.
void HIL::updateSubmit() {
  DMAFunction doRequest = [this](uint64_t beginAt, void *context) {
    auto pReq = (Request *)context;
    uint64_t tick = beginAt;
    pReq->beginAt = tick;

    debugprint(LOG_HIL,
               "TYPE %u  | REQ %7u | LCA %" PRIu64 " + %" PRIu64
               " | BYTE %" PRIu64 ", %" PRIu64,
               pReq->reqType, pReq->reqID, pReq->range.slpn, pReq->range.nlp,
               pReq->offset, pReq->length);

    ICL::Request reqInternal(*pReq);
    switch (pReq->reqType) {
      case READ:
        pICL->read(reqInternal, tick);
        stat.request[0]++;
        stat.iosize[0] += pReq->length;
        updateBusyTime(0, beginAt, tick);
        updateBusyTime(2, beginAt, tick);
        break;
      case WRITE:
        pICL->write(reqInternal, tick);
        stat.request[1]++;
        stat.iosize[1] += pReq->length;
        updateBusyTime(1, beginAt, tick);
        updateBusyTime(2, beginAt, tick);
        break;
      case TRIM:
        pICL->trim(reqInternal, tick);
        break;
      case FLUSH:
        pICL->flush(reqInternal, tick);
        break;
      case FORMAT:
        pICL->format(reqInternal, tick);
        break;
      default:
        panic(
            "Invalid HIL request type, tick %lu, queue size %lu+1, ID %lu, "
            "Type %u, Range %lu->%lu",
            tick, submitQueue.size(), pReq->reqID, pReq->reqType, pReq->offset,
            pReq->range.nlp);
        break;
    }

    pICL->updateSubmit();

    delete pReq;
  };

  while (submitQueue.size() > 0 && !pICL->submitQueueIsFull()) {
    // Yuda: no queue shceduling at HIL, FIFO
    auto req = submitQueue.front();
    submitQueue.pop_front();
    CPU::FUNCTION func = CPU::READ;
    switch (req.reqType) {
      case READ:
        func = CPU::READ;
        break;
      case WRITE:
        func = CPU::WRITE;
        break;
      case TRIM:
      case FLUSH:
      case FORMAT:
        func = CPU::FLUSH;
        break;
      default:
        panic("Invalid HIL request type %u", req.reqType);
        break;
    }
    // The commit queue is a temporary storage between submit and completion
    // queue.
    commitQueue.push_back(req);
    execute(CPU::HIL, func, doRequest, new Request(req));
  }
}

/// @brief This is the commit callback for ICL.
void HIL::Commit() {
  // firstly send remaining entries in submission queue
  updateSubmit();
  // update ICL commit queue, this method will move the comleted entries in
  // submit queue to commit queue.
  // Yuda: Temporarily. May change to other update ways (like proactive update
  // in ICL)
  pICL->updateCommit();
  ICL::Request iclreq;

  while (pICL->commitToUpper(iclreq)) {
    auto iter = commitQueue.begin();
    bool find = false;
    for (; iter != commitQueue.end(); ++iter) {
      if (iter->reqID == iclreq.reqID) {
        find = true;
        Request hilreq = *iter;
        commitQueue.erase(iter);
        if(!iclreq.readValidPage){
          CallBackContext *pContext = hilreq.context;
          pContext->readValidPage = false;
        }
        hilreq.beginAt = iclreq.beginAt;
        hilreq.finishedAt = iclreq.finishAt;
        debugprint(LOG_HIL, "HIL-fin,%lu,%d,%lu,%lu,%lu",
                   hilreq.reqID, hilreq.reqType, hilreq.beginAt, hilreq.finishedAt,
                   hilreq.finishedAt - hilreq.beginAt);
        completionQueue.push(hilreq);
        updateCompletion();
        break;
      }
    }
    if (!find) {
      panic("Unexpected ICL request ID %lu", iclreq.reqID);
    }
    // printf("HIL: @%lu commited ID=%lu, finishedAt=%lu\n", getTick(),
    // iclreq.reqID, iclreq.finishAt);
  }
  completion();
}
void HIL::read(Request &req) {
  Request newreq = req;
  newreq.reqID = ++reqCount;
  newreq.reqType = READ;
  submitQueue.push_back(newreq);
  updateSubmit();
}

void HIL::write(Request &req) {
  Request newreq = req;
  newreq.reqID = ++reqCount;
  newreq.reqType = WRITE;
  submitQueue.push_back(newreq);
  updateSubmit();
}

void HIL::flush(Request &req) {
  Request newreq = req;
  newreq.reqID = ++reqCount;
  newreq.reqType = FLUSH;
  submitQueue.push_back(newreq);
  updateSubmit();
}

void HIL::trim(Request &req) {
  Request newreq = req;
  newreq.reqID = ++reqCount;
  newreq.reqType = TRIM;
  submitQueue.push_back(newreq);
  updateSubmit();
}

void HIL::format(Request &req, bool erase) {
  Request newreq = req;
  newreq.reqID = ++reqCount;
  if (erase) {
    newreq.reqType = FORMAT;
  } else {
    newreq.reqType = TRIM;
  }
  submitQueue.push_back(newreq);
  updateSubmit();
}

void HIL::getLPNInfo(uint64_t &totalLogicalPages, uint32_t &logicalPageSize) {
  pICL->getLPNInfo(totalLogicalPages, logicalPageSize);
}

uint64_t HIL::getUsedPageCount(uint64_t lcaBegin, uint64_t lcaEnd) {
  return pICL->getUsedPageCount(lcaBegin, lcaEnd);
}

void HIL::updateBusyTime(int idx, uint64_t begin, uint64_t end) {
  if (end <= stat.lastBusyAt[idx]) {
    return;
  }

  if (begin < stat.lastBusyAt[idx]) {
    stat.busy[idx] += end - stat.lastBusyAt[idx];
  }
  else {
    stat.busy[idx] += end - begin;
  }

  stat.lastBusyAt[idx] = end;
}

void HIL::updateCompletion() {
  if (completionQueue.size() > 0) {
    if (lastScheduled != completionQueue.top().finishedAt) {
      lastScheduled = completionQueue.top().finishedAt;
      schedule(completionEvent, lastScheduled);
    }
  }
}

void HIL::completion() {
  uint64_t tick = getTick();

  while (completionQueue.size() > 0) {
    auto &req = completionQueue.top();

    if (req.finishedAt <= tick) {
      CallBackContext *pContext = (CallBackContext *)req.context;
      if(req.none){
        req.function(tick, pContext);
      }
      else{
        NVMe::IOContext * ioContext = (NVMe::IOContext *)pContext->context;
        ioContext->readValidPage = pContext->readValidPage;
        req.function(tick, pContext->context);
      }

      completionQueue.pop();
    }
    else {
      break;
    }
  }

  updateCompletion();
}

void HIL::getStatList(std::vector<Stats> &list, std::string prefix) {
  Stats temp;

  temp.name = prefix + "read.request_count";
  temp.desc = "Read request count";
  list.push_back(temp);

  temp.name = prefix + "read.bytes";
  temp.desc = "Read data size in byte";
  list.push_back(temp);

  temp.name = prefix + "read.busy";
  temp.desc = "Device busy time when read";
  list.push_back(temp);

  temp.name = prefix + "write.request_count";
  temp.desc = "Write request count";
  list.push_back(temp);

  temp.name = prefix + "write.bytes";
  temp.desc = "Write data size in byte";
  list.push_back(temp);

  temp.name = prefix + "write.busy";
  temp.desc = "Device busy time when write";
  list.push_back(temp);

  temp.name = prefix + "request_count";
  temp.desc = "Total request count";
  list.push_back(temp);

  temp.name = prefix + "bytes";
  temp.desc = "Total data size in byte";
  list.push_back(temp);

  temp.name = prefix + "busy";
  temp.desc = "Total device busy time";
  list.push_back(temp);

  pICL->getStatList(list, prefix);
}

void HIL::getStatValues(std::vector<double> &values) {
  values.push_back(stat.request[0]);
  values.push_back(stat.iosize[0]);
  values.push_back(stat.busy[0]);
  values.push_back(stat.request[1]);
  values.push_back(stat.iosize[1]);
  values.push_back(stat.busy[1]);
  values.push_back(stat.request[0] + stat.request[1]);
  values.push_back(stat.iosize[0] + stat.iosize[1]);
  values.push_back(stat.busy[2]);

  pICL->getStatValues(values);
}

void HIL::resetStatValues() {
  memset(&stat, 0, sizeof(stat));

  pICL->resetStatValues();
}

}  // namespace HIL

}  // namespace SimpleSSD
