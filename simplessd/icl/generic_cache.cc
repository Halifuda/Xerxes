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

#include "icl/generic_cache.hh"

#include <algorithm>
#include <cstddef>
#include <limits>

#include "icl/icl.hh"
#include "util/algorithm.hh"

#define USE_NEW_ICL 1
namespace SimpleSSD {

namespace ICL {

struct EvictInfoKey {
  uint64_t rID;
  uint64_t rSub;
  bool operator==(const EvictInfoKey &k) const {
    return rID == k.rID && rSub == k.rSub;
  }
};

struct EvictHash {
  size_t operator()(const EvictInfoKey &k) const {
    return 10607 * k.rID + 1607 * k.rSub;
  }
};

static std::unordered_map<EvictInfoKey, std::vector<Line *>, EvictHash> evictMap;

static void evictinfo_insert(const Request &req, Line *pLine) {
  EvictInfoKey k;
  k.rID = req.reqID;
  k.rSub = req.reqSubID;

  if (evictMap.count(k) == 0) {
    evictMap.insert({k, std::vector<Line *>{}});
  }
  auto &v = evictMap.at(k);
  v.push_back(pLine);
  pLine->evicting = true;
}

static void evictinfo_remove(const Request &req) {
  EvictInfoKey k;
  k.rID = req.reqID;
  k.rSub = req.reqSubID;

  if (evictMap.count(k) == 0) {
    return;
  }
  auto &v = evictMap.at(k);
  for (auto pl : v) {
    pl->evicting = false;
  }
}

GenericCache::GenericCache(ConfigReader &c, FTL::FTL *f, DRAM::AbstractDRAM *d)
    : AbstractCache(c, f, d),
      superPageSize(f->getInfo()->pageSize),
      parallelIO(f->getInfo()->pageCountToMaxPerf),
      lineCountInSuperPage(f->getInfo()->ioUnitInPage),
      mappingUnitInLine(f->getInfo()->subpageInUnit),
      lineCountInMaxIO(parallelIO * lineCountInSuperPage),
      waySize(conf.readUint(CONFIG_ICL, ICL_WAY_SIZE)),
      prefetchIOCount(conf.readUint(CONFIG_ICL, ICL_PREFETCH_COUNT)),
      prefetchIORatio(conf.readFloat(CONFIG_ICL, ICL_PREFETCH_RATIO)),
      useReadCaching(conf.readBoolean(CONFIG_ICL, ICL_USE_READ_CACHE)),
      useWriteCaching(conf.readBoolean(CONFIG_ICL, ICL_USE_WRITE_CACHE)),
      useReadPrefetch(conf.readBoolean(CONFIG_ICL, ICL_USE_READ_PREFETCH)),
      gen(rd()),
      dist(std::uniform_int_distribution<uint32_t>(0, waySize - 1)) {
  uint64_t cacheSize = conf.readUint(CONFIG_ICL, ICL_CACHE_SIZE);

  lineSize = superPageSize / lineCountInSuperPage;

  pFTL->setCommitCallback([this]() { Commit(); });

  if (lineSize != superPageSize) {
    bSuperPage = true;
  }

  lineSize = lineSize / mappingUnitInLine;

  if (!conf.readBoolean(CONFIG_FTL, FTL::FTL_USE_RANDOM_IO_TWEAK)) {
    lineSize = superPageSize;
    lineCountInSuperPage = 1;
    mappingUnitInLine = 1;
    lineCountInMaxIO = parallelIO;
  }

  // if (!useReadCaching && !useWriteCaching) {
  //   return;
  // }

  // Fully-associated?
  if (waySize == 0) {
    setSize = 1;
    waySize = MAX(cacheSize / lineSize, 1);
  } else {
    setSize = MAX(cacheSize / lineSize / waySize, 1);
  }

  debugprint(
      LOG_ICL_GENERIC_CACHE,
      "CREATE  | Set size %u | Way size %u | Line size %u | Capacity %" PRIu64,
      setSize, waySize, lineSize, (uint64_t)setSize * waySize * lineSize);
  debugprint(LOG_ICL_GENERIC_CACHE,
             "CREATE  | line count in super page %u | line count in max I/O %u",
             lineCountInSuperPage, lineCountInMaxIO);

  cacheData.resize(setSize);

  for (uint32_t i = 0; i < setSize; i++) {
    cacheData[i] = new Line[waySize]();
  }

  internalID = 0;
  evictData.resize(lineCountInSuperPage * mappingUnitInLine);

  prefetchTrigger = std::numeric_limits<uint64_t>::max();
  writePolicy = (WRITE_POLICY)conf.readInt(CONFIG_ICL, ICL_WRITE_POLICY);
  evictMode = (EVICT_MODE)conf.readInt(CONFIG_ICL, ICL_EVICT_GRANULARITY);
  prefetchMode =
      (PREFETCH_MODE)conf.readInt(CONFIG_ICL, ICL_PREFETCH_GRANULARITY);

  // Set evict policy functional
  policy = (EVICT_POLICY)conf.readInt(CONFIG_ICL, ICL_EVICT_POLICY);

  switch (policy) {
    case POLICY_RANDOM:
      evictFunction = [this](uint32_t, uint64_t &) -> uint32_t {
        return dist(gen);
      };
      compareFunction = [this](Line *a, Line *b) -> Line * {
        if (a && b) {
          return dist(gen) > waySize / 2 ? a : b;
        } else if (a || b) {
          return a ? a : b;
        } else {
          return nullptr;
        }
      };

      break;
    case POLICY_FIFO:
      evictFunction = [this](uint32_t setIdx, uint64_t &tick) -> uint32_t {
        uint32_t wayIdx = 0;
        uint64_t min = std::numeric_limits<uint64_t>::max();

        for (uint32_t i = 0; i < waySize; i++) {
          tick += getCacheLatency() * 8;
          // pDRAM->read(MAKE_META_ADDR(setIdx, i, offsetof(Line, insertedAt)),
          // 8, tick);

          if (cacheData[setIdx][i].insertedAt < min) {
            min = cacheData[setIdx][i].insertedAt;
            wayIdx = i;
          }
        }

        return wayIdx;
      };
      compareFunction = [](Line *a, Line *b) -> Line * {
        if (a && b) {
          if (a->insertedAt < b->insertedAt) {
            return a;
          } else {
            return b;
          }
        } else if (a || b) {
          return a ? a : b;
        } else {
          return nullptr;
        }
      };

      break;
    case POLICY_LEAST_RECENTLY_USED:
      evictFunction = [this](uint32_t setIdx, uint64_t &tick) -> uint32_t {
        uint32_t wayIdx = 0;
        uint64_t min = std::numeric_limits<uint64_t>::max();

        for (uint32_t i = 0; i < waySize; i++) {
          tick += getCacheLatency() * 8;
          // pDRAM->read(MAKE_META_ADDR(setIdx, i, offsetof(Line,
          // lastAccessed)), 8, tick);

          if (cacheData[setIdx][i].lastAccessed < min) {
            min = cacheData[setIdx][i].lastAccessed;
            wayIdx = i;
          }
        }

        return wayIdx;
      };
      compareFunction = [](Line *a, Line *b) -> Line * {
        if (a && b) {
          if (a->lastAccessed < b->lastAccessed) {
            return a;
          } else {
            return b;
          }
        } else if (a || b) {
          return a ? a : b;
        } else {
          return nullptr;
        }
      };

      break;
    default:
      panic("Undefined cache evict policy");

      break;
  }

  memset(&stat, 0, sizeof(stat));
}

GenericCache::~GenericCache() {
  if (!useReadCaching && !useWriteCaching) {
    return;
  }

  for (uint32_t i = 0; i < setSize; i++) {
    delete[] cacheData[i];
  }

}

#if USE_NEW_ICL
/// AYDNOTICE: cacheUpdateContext *up will be deleted in this call, please
/// DONOT delete manually!
void GenericCache::updateCacheline(uint64_t tick, cacheUpdateContext *up) {
  if (up != nullptr) {
    auto pl = up->pLine;
    if (pl != nullptr) {
      // AYDNOTICE: check if there is dirty LCA
      auto set = calcSetIndex(up->lca);
      uint64_t findV = tick;
      auto way = getValidWay(up->lca, up->subIdx, findV);
      bool found = way != waySize;
      if (found) { 
        auto npl = &cacheData[set][way];
        // AYDNOTICE: throw undirty update over dirty way
        if (npl->dirty && !pl->dirty) {
          return;
        }
        tick = findV;
        pl = npl;
      }

      pl->insertedAt = tick;
      pl->lastAccessed = tick;
      pl->valid = up->valid;
      pl->dirty = up->dirty;
      pl->waiting = up->waiting;
      pl->tag = up->tag;
      pl->subIdx = up->subIdx;
    }
    delete up;
  }
}
void GenericCache::updateContext(cacheReqContext &c) {
  // debugprint(LOG_ICL_GENERIC_CACHE, "update id=%lu-%lu waiting%d, size%lu",
  //            c.req.reqID, c.req.reqSubID, c.iswaiting, c.ftllist.size());
  if (c.iswaiting) {
    // restore waiting state
    if (c.ftllist.empty()) {
      c.iswaiting = false;
    } else {
      return;
    }
  }
  // This case we need not to wait ftl commit
  if (c.ftllist.empty()) {
    bool wait = false;
    // Iteratively do steps
    while (!wait && !c.steps.empty()) {
      auto f = c.steps.front();
      c.steps.pop_front();
      wait = f(c.req.finishAt, &c);
      // Check whether there are reqs
      if (wait) wait = !c.ftllist.empty();
    }
    // There are ftl reqs
    if (!c.ftllist.empty()) {
      c.iswaiting = true;
      auto iter = c.ftllist.begin();
      while (iter != c.ftllist.end()) {
        auto cur = iter;
        iter++;
        auto &ftlReq = cur->second.first;
        auto tick = c.req.finishAt;
        ftlReq.beginAt = tick;
        switch (ftlReq.reqType) {
          case READ:
            if (!coalesce_CheckOrInsert(ftlReq)) pFTL->read(ftlReq, tick);
            break;
          case WRITE:
            pFTL->write(ftlReq, tick);
            break;
          case TRIM:
            pFTL->trim(ftlReq, tick);
            break;
          case FORMAT:
            // AYDTODO: format currently is sync
            break;
          default:
            panic("Invalid FTL request type in Generic Cache, TYPE=%d",
                  ftlReq.reqType);
        }
      }
    }
  }
  // Else just wait
}
#endif

#if USE_NEW_ICL
bool GenericCache::evictCheckHasNoEvictingInSet(Request *req) {
  // if (req->reqID == 35) {
  //   for (uint32_t way = 0; way < waySize; ++way) printf("w%2u-%d\n", way, cacheData[0][way].evicting);
  // }
  auto set = calcSetIndex(req->range.slpn);
  for (uint32_t way = 0; way < waySize; ++way) {
    if (cacheData[set][way].evicting == false) {
      return true;
    }
  }
  return false;
}

void GenericCache::updateSubmit(bool toftl) {
  bool have_commit = false;
  auto iter = submitQueue.begin();
  std::vector<ReqKey> erase(0);
  while (iter != submitQueue.end()) {
    auto &ctxt = iter->second;
    if ((ctxt.req.reqType == WRITE && useWriteCaching) ||
        (ctxt.req.reqType == READ && useReadCaching)) {
      if (!evictCheckHasNoEvictingInSet(&ctxt.req)) {
        ++iter;
        continue;
      }
    }
    ctxt.req.finishAt = getTick();
    updateContext(ctxt);
    if (ctxt.steps.empty() && ctxt.ftllist.empty()) {
      if(!ctxt.req.internal){
        have_commit = true;
        commitQueue.push_back(ctxt.req);
        if (ctxt.req.reqType == WRITE && useWriteCaching)
          evictinfo_remove(ctxt.req);
      }
      erase.push_back(ReqKey(ctxt.req));
    }
    ++iter;
  }
  for (auto &e : erase) {
    submitQueue.erase(e);
  }
  if (toftl) pFTL->updateSubmit();
  if (have_commit) commitCallback();
}

void GenericCache::Commit() {
  FTL::Request ftlReq(0);
  ReqKey key;
  cacheReqContext *ctxt = nullptr;
  cacheUpdateContext *up = nullptr;
  while (pFTL->commitToUpper(ftlReq)) {
    bool find = false;
    key = ReqKey(ftlReq.reqID, ftlReq.reqSubID);

    if (submitQueue.find(key) == submitQueue.end()) {
      find = false;
    } else {
      auto KViter = submitQueue.find(key);
      ctxt = &KViter->second;
      auto &flist = ctxt->ftllist;
      auto ftliter = flist.find(ftlReq.ftlSubID);
      if (ftliter == flist.end()) {
        find = false;
      } else {
        find = true;
        up = ftliter->second.second;
      }
    }
    if (!find) {
      // SY: a ftl request may serve several cacheline
      // uint32_t findCount = mappingUnitInLine;
      for (auto i = evictQueue.begin(); i != evictQueue.end(); ++i) {
        if (i->ftlReq.reqID == ftlReq.reqID &&
            i->ftlReq.reqSubID == ftlReq.reqSubID &&
            i->ftlReq.ftlSubID == ftlReq.ftlSubID) {
          find = true;
          // AYDTODO: For simplicity we update evictData[] immediately
          // in evict method, so (1) we only record 1 entry here for 1
          // ftl req; (2) we do not update anything.
          evictQueue.erase(i);
          break;
        }
      }
      if (!find) {
        // AYDNOTICE: FTL trim will not wait for PAL, so it actually
        // performs sync return. Then, Commit() and updateSubmit()
        // will conflict (double erase).
        // If we donot add FTL trim to wait queue (sync), it will
        // eventually reach here, and so does FTL format.
        // So we will not panic under such condition.
        if (ftlReq.reqType == TRIM) {
          debugprint(LOG_ICL_GENERIC_CACHE,
                     "Generic Cache receive FTL TRIM commit, ID=%lu-%lu",
                     ftlReq.reqID, ftlReq.reqSubID);
        } else if (ftlReq.reqType == FORMAT) {
          debugprint(LOG_ICL_GENERIC_CACHE,
                     "Generic Cache receive FTL FORMAT commit, ID=%lu-%lu",
                     ftlReq.reqID, ftlReq.reqSubID);
        } else {
          // AYDNOTICE: donot panic, ignore for there is some FTL req
          // that are ignored (like in write through)
          warn(
              "Generic cache received unknown FTL request, TYPE=%d, ID=%lu-%lu",
              ftlReq.reqType, ftlReq.reqID, ftlReq.reqSubID);
        }
      }
    } else {
      //SYTODO: write other subage to cache
      // We have a sub request sent this ftl req.
      ctxt->ftllist.erase(ftlReq.ftlSubID);
      ctxt->req.finishAt = MAX(ctxt->req.finishAt, ftlReq.finishAt);
      debugprint(LOG_ICL_GENERIC_CACHE,
                 "FTL commit ID=%lu-%lu-%lu, @%lu-%lu (%lu), remain %lu",
                 ftlReq.reqID, ftlReq.reqSubID, ftlReq.ftlSubID, ftlReq.beginAt,
                 ftlReq.finishAt, ftlReq.finishAt - ftlReq.beginAt,
                 ctxt->ftllist.size());
      updateCacheline(ftlReq.finishAt, up);
      updateContext(*ctxt);
      // write other subpage to cache
      if(ftlReq.reqType == READ && useReadCaching){
        pDRAM->write(&cacheData[ctxt->setIdx][ctxt->wayIdx], 
          ctxt->req.length, ctxt->req.finishAt);
        for(uint32_t i = 0; i < ftlReq.backInfo.size(); i++){
          if(ftlReq.backInfo[i].lpn == ftlReq.reqInfo[0].lpn &&
            ftlReq.backInfo[i].idx == ftlReq.reqInfo[0].idx &&
            ftlReq.backInfo[i].subIdx == ftlReq.reqInfo[0].subIdx){
              continue;
          }
          FTL::Request checkreq;
          checkreq.reqInfo.push_back(ftlReq.backInfo[i]);
          if(!coalesce_Check(checkreq)){
            Request writeReq;
            writeReq.reqID = 0;
            writeReq.reqSubID = internalID++;
            writeReq.reqType = WRITE;
            writeReq.range = ctxt->req.range;
            writeReq.range.slpn = ftlReq.backInfo[i].lpn * lineCountInSuperPage
                                + ftlReq.backInfo[i].idx;
            writeReq.length = lineSize;
            writeReq.subIdx = ftlReq.backInfo[i].subIdx;
            writeReq.internal = true;
            writeInternal(writeReq, ftlReq.finishAt, true);
          }
        }
      }

      if(ftlReq.readValidPage){
        ctxt->req.readValidPage = true;
      }
      // auto did = ctxt->req.reqID;
      // auto dsd = ctxt->req.reqSubID;
      // debugprint(LOG_ICL_GENERIC_CACHE, "Commit id=%lu-%lu, steps=%lu,
      // ftl=%lu",
      //            did, dsd, ctxt->steps.size(), ctxt->ftllist.size());

      if (ctxt->steps.empty() && ctxt->ftllist.empty()) {
        commitQueue.push_back(ctxt->req);
        if (ctxt->req.reqType == WRITE && useWriteCaching)
          evictinfo_remove(ctxt->req);
        submitQueue.erase(key);
      }
      coalesce_FTLCommit(ftlReq);
    }
  }
  updateSubmit(true);
  if (!commitQueue.empty()) commitCallback();
}

bool GenericCache::commitToUpper(Request &req) {
  if (commitQueue.empty()) {
    return false;
  }
  auto commit = commitQueue.front();
  commitQueue.pop_front();
  req = commit;
  return true;
}
#endif

bool GenericCache::coalesce_CheckOrInsert(FTL::Request &read) {
  if (read.reqType != READ) {
    return false;
  }
  auto lca = coalesce_getReqLCA(read);
  auto have = FTLReadCoalesceMap.count(lca);
  if (have) {
    auto entry = CoalescedReq{ReqKey{read.reqID, read.reqSubID}, read.ftlSubID};
    FTLReadCoalesceMap.at(lca).push_back(entry);
    auto origin = FTLReadCoalesceMap.at(lca).front();
    debugprint(LOG_ICL_GENERIC_CACHE,
               "FTL Read %lu-%lu-%lu-LCA%lu coalesced to %lu-%lu-%lu",
               read.reqID, read.reqSubID, read.ftlSubID, lca,
               origin.iclReqId.id, origin.iclReqId.subid, origin.ftlReqId);
    return true;
  } else {
    auto entry = CoalescedReq{ReqKey{read.reqID, read.reqSubID}, read.ftlSubID};
    // insert the entry to let other reqs know the origin req
    FTLReadCoalesceMap.insert(
        std::make_pair(lca, std::vector<CoalescedReq>(1, entry)));
    debugprint(LOG_ICL_GENERIC_CACHE,
               "New coalescing info recorded: FTL Read %lu-%lu-%lu-LCA%lu",
               read.reqID, read.reqSubID, read.ftlSubID, lca);
    return false;
  }
}

bool GenericCache::coalesce_Check(FTL::Request &read) {
  auto lca = coalesce_getReqLCA(read);
  auto have = FTLReadCoalesceMap.count(lca);
  if (have) {
    return true;
  } else {
    return false;
  }
}

void GenericCache::coalesce_FTLCommit(FTL::Request &commit) {
  if (commit.reqType != READ) {
    return;
  }
  auto lca = coalesce_getReqLCA(commit);
  auto have = FTLReadCoalesceMap.count(lca);
  if (have) {
    auto &list = FTLReadCoalesceMap.at(lca);
    for (auto entry : list) {
      if (entry.iclReqId.id == commit.reqID &&
          entry.iclReqId.subid == commit.reqSubID &&
          entry.ftlReqId == commit.ftlSubID) {
        continue;
      }
      auto &iclreq = submitQueue.at(entry.iclReqId);
      if (iclreq.ftllist.count(entry.ftlReqId) > 0) {
        auto f = iclreq.ftllist.at(entry.ftlReqId).first;
        debugprint(LOG_ICL_GENERIC_CACHE,
                   "coalesced FTL Read %lu-%lu-%lu finished due to commit of "
                   "%lu-%lu-%lu", 
                   f.reqID, f.reqSubID, f.ftlSubID, 
                   commit.reqID, commit.reqSubID, commit.ftlSubID);
        iclreq.ftllist.erase(entry.ftlReqId);
        iclreq.req.finishAt = MAX(iclreq.req.finishAt, commit.finishAt);
        updateContext(iclreq);
        if (iclreq.steps.empty() && iclreq.ftllist.empty()) {
          commitQueue.push_back(iclreq.req);
          submitQueue.erase(entry.iclReqId);
        }
      }
    }
    FTLReadCoalesceMap.erase(lca);
  }  // else return
}

uint64_t GenericCache::coalesce_getReqLCA(FTL::Request &req) {
  uint64_t index = req.reqInfo[0].lpn * lineCountInSuperPage;
  index = (index + req.reqInfo[0].idx) * mappingUnitInLine + req.reqInfo[0].subIdx;
  return index; // pass compile
}

uint64_t GenericCache::getCacheLatency() {
  static uint64_t latency = conf.readUint(CONFIG_ICL, ICL_CACHE_LATENCY);
  static uint64_t core = conf.readUint(CONFIG_CPU, CPU::CPU_CORE_ICL);

  return (core == 0) ? 0 : latency / core;
}

uint32_t GenericCache::calcSetIndex(uint64_t lca) { return lca % setSize; }

// AYDTODO: lca is passed as cacheData->tag, which is logic page number,
// we hope row is the subpage index, so we add an arg subIdx
void GenericCache::calcIOPosition(uint64_t lca, uint64_t subIdx, uint32_t &row) {
  lca %= lineCountInSuperPage * mappingUnitInLine;
  lca *= lineCountInSuperPage;
  // Further mod to avoid overflow
  row = lca + subIdx;
  row %= lineCountInSuperPage * mappingUnitInLine;


}

uint32_t GenericCache::getEmptyWay(uint32_t setIdx, uint64_t &tick) {
  uint32_t retIdx = waySize;
  uint64_t minInsertedAt = std::numeric_limits<uint64_t>::max();

  for (uint32_t wayIdx = 0; wayIdx < waySize; wayIdx++) {
    Line &line = cacheData[setIdx][wayIdx];

    if (!line.valid) {
      tick += getCacheLatency() * 8;
      // pDRAM->read(MAKE_META_ADDR(setIdx, wayIdx, offsetof(Line, insertedAt)),
      // 8, tick);

      if (minInsertedAt > line.insertedAt) {
        minInsertedAt = line.insertedAt;
        retIdx = wayIdx;
      }
    }
  }

  return retIdx;
}

uint32_t GenericCache::getValidWay(uint64_t lca, uint32_t subIdx, uint64_t &tick) {
  uint32_t setIdx = calcSetIndex(lca);
  uint32_t wayIdx;

  for (wayIdx = 0; wayIdx < waySize; wayIdx++) {
    Line &line = cacheData[setIdx][wayIdx];

    tick += getCacheLatency() * 8;
    // pDRAM->read(MAKE_META_ADDR(setIdx, wayIdx, offsetof(Line, tag)), 8,
    // tick);

    if (line.valid && line.tag == lca && line.subIdx == subIdx) {
      break;
    }
  }

  return wayIdx;
}

// select cache line for evict
bool GenericCache::caculateEvictLine(uint32_t setToFlush, uint64_t &tick){
  uint32_t cachelineNum = mappingUnitInLine * lineCountInSuperPage;
  uint32_t setIdx, wayIdx, row;
  uint32_t now = 0;
  uint32_t num = 0;

  // ensure each evictData will not be nullptr
  for (setIdx = 0; setIdx < setSize; setIdx++) {
    for (wayIdx = 0; wayIdx < waySize; wayIdx++) {
      if (cacheData[setIdx][wayIdx].valid && !cacheData[setIdx][wayIdx].dirty) {
        calcIOPosition(cacheData[setIdx][wayIdx].tag, cacheData[setIdx][wayIdx].subIdx, row);
        evictData[row] = compareFunction(evictData[row], cacheData[setIdx] + wayIdx);
      }
    }
  }
  
  bool have = false;
  for (now = 0; now < cachelineNum; now++){
    if(evictData[now]){

      if(calcSetIndex(evictData[now]->tag) == setToFlush){
        have = true;
      }
      evictData[now]->valid = false;
      evictData[now]->tag = 0;

      evictData[now]->insertedAt = tick;
      evictData[now]->lastAccessed = tick;
      evictData[now]->dirty = false;
      evictData[now] = nullptr;
      num++;
    }
  }

  if(!have){
    Line *pLineToFlush = nullptr;
    for (wayIdx = 0; wayIdx < waySize; wayIdx++) {
      if (cacheData[setToFlush][wayIdx].valid && !cacheData[setToFlush][wayIdx].dirty) {
        pLineToFlush =
          compareFunction(pLineToFlush, cacheData[setToFlush] + wayIdx);
      }
    }

    if (pLineToFlush) {
      pLineToFlush->valid = false;
      pLineToFlush->tag = 0;

      pLineToFlush->insertedAt = tick;
      pLineToFlush->lastAccessed = tick;
      pLineToFlush->dirty = false;
      pLineToFlush = nullptr;
    }
  }
  // if there are enough clean(dirty = false) cacheline
  // just flush them and no ftl write
  // AYDTODO: num >= waySize is OK, only #waySize are checked above.
  if(num >= cachelineNum){
    debugprint(LOG_ICL_GENERIC_CACHE, "No need for evict");
    return false;
  }

  setIdx = setToFlush;
  now = 0;
  for (wayIdx = 0; wayIdx < waySize; wayIdx++){
    if(cacheData[setIdx][wayIdx].valid && cacheData[setIdx][wayIdx].dirty){
      evictData[now] = compareFunction(evictData[now], cacheData[setIdx] + wayIdx);
      now = (now + 1) % cachelineNum;
    }
  }


  for (setIdx = 0; setIdx < setSize; setIdx++) {
    if(setIdx == setToFlush){
      continue;
    }
    for (wayIdx = 0; wayIdx < waySize; wayIdx++) {
      if (cacheData[setIdx][wayIdx].valid && cacheData[setIdx][wayIdx].dirty) {
        calcIOPosition(cacheData[setIdx][wayIdx].tag, cacheData[setIdx][wayIdx].subIdx, row);
        evictData[row] = compareFunction(evictData[row], cacheData[setIdx] + wayIdx);
      }
    }
  }

  have = false;
  for(uint32_t i = 0; i < cachelineNum; i++){
    if(evictData[i] && calcSetIndex(evictData[i]->tag) == setToFlush){
      have = true;
    }
  }

  // ensure the setToFlush to evict enough way
  if(!have){
    Line *pLineToFlush = nullptr;
    for (wayIdx = 0; wayIdx < waySize; wayIdx++) {
      if (cacheData[setToFlush][wayIdx].valid && cacheData[setToFlush][wayIdx].dirty) {
        pLineToFlush =
          compareFunction(pLineToFlush, cacheData[setToFlush] + wayIdx);
      }
    }

    if (pLineToFlush) {
      calcIOPosition(pLineToFlush->tag, pLineToFlush->subIdx, row);
      evictData[row] = pLineToFlush;
    }
  }
  tick += getCacheLatency() * setSize * waySize * 8;
  return true;
}

void GenericCache::checkSequential(Request &req, SequentialDetect &data) {
  if (data.lastRequest.reqID == req.reqID) {
    data.lastRequest.range = req.range;
    data.lastRequest.offset = req.offset;
    data.lastRequest.length = req.length;

    return;
  }


  if (data.lastRequest.range.slpn * lineSize * mappingUnitInLine +
          data.lastRequest.offset + data.lastRequest.length ==
      req.range.slpn * lineSize * mappingUnitInLine + req.offset) {
    if (!data.enabled) {
      data.hitCounter++;
      data.accessCounter += data.lastRequest.offset + data.lastRequest.length;

      if (data.hitCounter >= prefetchIOCount &&
          (float)data.accessCounter / superPageSize >= prefetchIORatio) {
        data.enabled = true;
      }
    }
  } else {
    data.enabled = false;
    data.hitCounter = 0;
    data.accessCounter = 0;
  }

  data.lastRequest = req;
}

void GenericCache::evictCache(Request *req, cacheReqContext *context, uint64_t &ftlSubID, uint64_t tick,
                              bool flush) {
  FTL::Request reqInternal(lineCountInSuperPage, mappingUnitInLine, *req);
  uint64_t beginAt;
  uint64_t finishedAt = tick;

  debugprint(LOG_ICL_GENERIC_CACHE, "----- | Begin eviction");
  uint32_t i = 0;
  for (uint32_t row = 0; row < lineCountInSuperPage; row++) {
    beginAt = tick;
    reqInternal.ftlSubID = ftlSubID++;
    bool flush_this_page = true;
    for (uint32_t idx = 0; idx < mappingUnitInLine; idx++) {
      i = row * mappingUnitInLine + idx;

      if (evictData[i] == nullptr) {
        // AYDTODO: if we has data less than a page,
        // do not evict at current tick. Just skip.
        flush_this_page = false;
        reqInternal.reqInfo[idx].subIdx = mappingUnitInLine;
        // so many warns...
        // warn("Empty evictData row=%u", i);
        break;
      }

      if (evictData[i]->valid && evictData[i]->dirty) {
        reqInternal.reqInfo[idx].lpn = evictData[i]->tag / lineCountInSuperPage;
        reqInternal.reqInfo[idx].idx = row;
        reqInternal.reqInfo[idx].subIdx = evictData[i]->subIdx;

        reqInternal.reqType = WRITE;
        debugprint(LOG_ICL_GENERIC_CACHE, "      | evict %lu %u %u", 
              reqInternal.reqInfo[idx].lpn,
              reqInternal.reqInfo[idx].idx, 
              reqInternal.reqInfo[idx].subIdx);
        // AYDTODO: We do not update evictData[] when evict commit, but
        // update immediately. So we only record 1 ftl req for 1 page
        // auto evict = evictContext(reqInternal, flush, &evictData[i]);
        // evictQueue.push_back(evict);
      }

      if (flush) {
        evictData[i]->valid = false;
        evictData[i]->tag = 0;
        evictData[i]->subIdx = 0;
      }

      evictData[i]->insertedAt = beginAt;
      evictData[i]->lastAccessed = beginAt;
      evictData[i]->dirty = false;
      evictData[i]->evicting = true;

      if (useWriteCaching) evictinfo_insert(*req, evictData[i]);
      // AYDTODO: immediately set (see above)
      evictData[i] = nullptr;
    }
    if (!flush_this_page) {
      continue;
    }
    // evictQueue.push_back(evictContext(reqInternal, flush, nullptr));
    // pFTL->write(reqInternal, beginAt);
    reqInternal.beginAt = beginAt;
    context->ftllist.insert({reqInternal.ftlSubID, {reqInternal, nullptr}});
    finishedAt = MAX(finishedAt, beginAt);
  }

  debugprint(LOG_ICL_GENERIC_CACHE,
             "----- | End eviction | %" PRIu64 " - %" PRIu64 " (%" PRIu64 ")",
             tick, finishedAt, finishedAt - tick);
}

#if USE_NEW_ICL
#define REQFUNC [this](uint64_t beginAt, cacheReqContext * c) -> bool
#define REQFUNCU [this](uint64_t, cacheReqContext * c) -> bool

void GenericCache::read(Request &req, uint64_t &tick){
  /*debugprint(LOG_ICL_GENERIC_CACHE,
             "READ  | REQ %7u-%-4u | LCA %" PRIu64 " | SIZE %" PRIu64,
             req.reqID, req.reqSubID, req.range.slpn, req.length);*/

  if (useReadCaching) {

    uint64_t length = req.length;
    uint64_t offset = req.offset;
    uint64_t reqRemain = req.length;
    uint64_t beginLine = req.offset / lineSize;
    uint64_t nLine = (req.offset + req.length + lineSize - 1) / lineSize;
    uint64_t finishedAt = tick;
    uint64_t beginAt;

    if (useReadPrefetch) {
      checkSequential(req, readDetect);
    }

    req.offset = req.offset % lineSize;

    for(uint64_t i = beginLine; i < nLine; i++){
      beginAt = tick;
      req.subIdx = i;
      req.length = MIN(reqRemain, lineSize - req.offset);
      req.reqSubID++;
      readInternal(req, beginAt);
      req.offset = 0;
      reqRemain -= req.length;
      finishedAt = MAX(beginAt, finishedAt);
    }

    req.length = length;
    req.offset = offset;
  }

  else{
    req.reqSubID++;
    auto key = ReqKey(req);
    submitQueue.insert({key, cacheReqContext()});
    auto &context = submitQueue.at(key);
    context.ftlReqId = 0;
    context.req = req;
    if (context.req.finishAt < context.req.beginAt)
      context.req.finishAt = context.req.beginAt;
    context.arrived = tick;

    debugprint(LOG_ICL_GENERIC_CACHE,
              "READ  | REQ %7u-%-4u | LCA %" PRIu64 " | SIZE %" PRIu64,
              req.reqID, req.reqSubID, req.range.slpn, req.length);


    context.steps.push_back(REQFUNC {
      uint64_t tick = beginAt;
      pDRAM->write(nullptr, c->req.length, tick);
      c->req.finishAt = tick;
      return false;
    });
    context.steps.push_back(REQFUNC {
      FTL::Request ftlReq(lineCountInSuperPage, mappingUnitInLine, c->req);
      ftlReq.beginAt = beginAt;
      ftlReq.ftlSubID = c->ftlReqId++;
      c->ftllist.insert({ftlReq.ftlSubID, {ftlReq, nullptr}});
      return true;
    });
    context.steps.push_back(REQFUNCU {
      stat.request[0]++;
      c->hit = false;
      if (c->hit) stat.cache[0]++;
      return false;
    });
  }
}

bool GenericCache::readInternal(Request &req, uint64_t &tick) {
  auto key = ReqKey(req);
  submitQueue.insert({key, cacheReqContext()});
  auto &context = submitQueue.at(key);
  context.ftlReqId = 0;
  context.req = req;
  if (context.req.finishAt < context.req.beginAt)
    context.req.finishAt = context.req.beginAt;
  context.arrived = tick;

  debugprint(LOG_ICL_GENERIC_CACHE,
             "READ  | REQ %7u-%-4u | LCA %" PRIu64 " | SUBPAGE %u | SIZE %" PRIu64,
             req.reqID, req.reqSubID, req.range.slpn, req.subIdx, req.length);

  uint64_t LCA = context.req.range.slpn;

  bool prefetch = false;
  if (useReadPrefetch) {
    context.is_sequential = readDetect.enabled;
    prefetch = LCA == prefetchTrigger;
  }
  // check cache hit
  context.setIdx = calcSetIndex(LCA);
  context.wayIdx = getValidWay(LCA, context.req.subIdx, tick);
  context.hit = context.wayIdx != waySize;
  context.to_ftl = (!context.hit) || prefetch;
  // backup tick
  context.req.finishAt = tick;

  // main ReqFunc, access DRAM and FTL (is should)
  context.steps.push_back(REQFUNC {
    // hit
    if (c->hit) {
      Line *pl = &cacheData[c->setIdx][c->wayIdx];
      if (!pl->waiting) {
        uint64_t hitTick = beginAt;
        if (hitTick < pl->insertedAt) hitTick = pl->insertedAt;
        pl->lastAccessed = hitTick;
        pDRAM->read(pl, c->req.length, hitTick);
        // update finish tick
        c->req.finishAt = MAX(c->req.finishAt, hitTick);

        debugprint(LOG_ICL_GENERIC_CACHE,
                     "READ  | Cache hit at (%u, %u) | %" PRIu64 " - %" PRIu64
                     " (%" PRIu64 ")",
                     c->setIdx, c->wayIdx, c->arrived, hitTick,
                     hitTick - c->arrived);
      }
      else {
        // we count on ftl coalescing policy to avoid double read, 
        // here we force the ICL req to send its FTL reads.
        c->to_ftl = true;
        c->hit = false;
        // Still need to update lastAccessed
        uint64_t hitTick = beginAt;
        if (hitTick < pl->insertedAt) hitTick = pl->insertedAt;
        pl->lastAccessed = hitTick;
        c->req.finishAt = MAX(c->req.finishAt, hitTick);

        debugprint(LOG_ICL_GENERIC_CACHE, 
                     "READ  | Cache hit at (%u, %u) | waiting for FTL commit", 
                     c->setIdx, c->wayIdx);
      }
    }
    // to_ftl in READ means need read FTL
    if (c->to_ftl) {
      uint32_t set, way;
      uint64_t beginLCA, endLCA, tick, finishTick;
      FTL::Request ftlreq(lineCountInSuperPage, mappingUnitInLine,c->req);

      if (c->is_sequential) {
        if (c->hit) {
          debugprint(LOG_ICL_GENERIC_CACHE, 
                "READ  | REQ %7u-%-4u | Prefetch triggered", 
                c->req.reqID, c->req.reqSubID);
        } else {
          debugprint(LOG_ICL_GENERIC_CACHE, 
                "READ  | REQ %7u-%-4u | Read ahead triggered", 
                c->req.reqID, c->req.reqSubID);
        }

        // TEMP: Disable DRAM calculation for prevent conflict
        // AYDTODO: is setScheduling() necessary?
        // pDRAM->setScheduling(false);

        beginLCA = c->req.range.slpn;
        // If super-page is disabled, just read all pages from all planes
        if (prefetchMode == MODE_ALL || !bSuperPage) {
          endLCA = beginLCA + lineCountInMaxIO;
          prefetchTrigger = beginLCA + lineCountInMaxIO / 2;
        } else {
          endLCA = beginLCA + lineCountInSuperPage;
          prefetchTrigger = beginLCA + lineCountInSuperPage / 2;
        }
        lastPrefetched = endLCA;
      } else {
        beginLCA = c->req.range.slpn;
        endLCA = beginLCA + 1;
      }

      // generate FTL requests
      finishTick = beginAt;
      ftlreq.reqInfo.clear();
      ftlreq.reqInfo.push_back(FTL::ReqInfo(0, 0, 0));
      if(!c->hit){
        tick = beginAt;
        auto _faketick = tick;
        set = calcSetIndex(c->req.range.slpn);
        // AYDNOTICE: get valid way, this code block also handles
        // hit-but-waiting-previous-FTL-to-commit condition.
        way = getValidWay(c->req.range.slpn, c->req.subIdx, _faketick);
        if (way == waySize) {
          way = getEmptyWay(set, tick);
          if (way == waySize){
            if(caculateEvictLine(set, tick)) {
              evictCache(&c->req, c, c->ftlReqId, tick, true);
            }
            way = getEmptyWay(set, tick);
          }
        }

        uint64_t lca = c->req.range.slpn;

        ftlreq.reqInfo[0].lpn = lca / lineCountInSuperPage;
        ftlreq.reqInfo[0].idx = lca % lineCountInSuperPage;
        ftlreq.reqInfo[0].subIdx = c->req.subIdx;
        ftlreq.ftlSubID = c->ftlReqId++;
        // set update context
        auto up = new cacheUpdateContext;
        up->dirty = false, up->valid = true;
        // up is used when commit, set to false
        up->waiting = false;
        up->lca = up->tag = lca;
        up->pLine = &cacheData[set][way];
        up->set = set, up->way = way;
        up->subIdx = c->req.subIdx;

        // AYDNOTICE: force update, and set the waiting flag
        auto now = new cacheUpdateContext;
        *now = *up;
        // now is used immediately, set to true
        now->waiting = true;
        updateCacheline(tick, now);

        ftlreq.beginAt = tick;
        c->ftllist.insert({ftlreq.ftlSubID, {ftlreq, up}});

        debugprint(LOG_ICL_GENERIC_CACHE, "READ  | Cache miss at (%u, %u)",
                     set, way);
        finishTick = max(finishTick, tick);
        if(!readDetect.enabled){
          beginLCA++;
        }
      }
      for (uint64_t lca = beginLCA; lca < endLCA; lca++) {
        for(uint32_t i = 0; i < mappingUnitInLine; i++){
          tick = beginAt;
          // Check cache
          if (getValidWay(lca, i, tick) != waySize) {
            continue;
          }
          // Find way to write data read from NVM
          set = calcSetIndex(lca);
          way = getEmptyWay(set, tick);
          if (way == waySize) {

            if(caculateEvictLine(set, tick)){
              evictCache(&c->req, c, c->ftlReqId, tick, true);
            }
            way = getEmptyWay(set, tick);
          }

          // AYDTODO: codes below correct?
          // cacheData[set][way].insertedAt = tick;
          // cacheData[set][way].lastAccessed = tick;
          // cacheData[set][way].valid = true;
          //cacheData[set][way].subIdx = mappingUnitInLine;
          // cacheData[set][way].dirty = false;

          ftlreq.reqInfo[0].lpn = lca / lineCountInSuperPage;
          ftlreq.reqInfo[0].idx = lca % lineCountInSuperPage;
          ftlreq.reqInfo[0].subIdx = i;
          ftlreq.ftlSubID = c->ftlReqId++;
          // set update context
          auto up = new cacheUpdateContext;
          up->dirty = false, up->valid = true;
          // up is used when commit, set to false
          up->waiting = false;
          up->lca = up->tag = lca;
          up->pLine = &cacheData[set][way];
          up->set = set, up->way = way;
          up->subIdx = i;

          // AYDNOTICE: force update, and set the waiting flag
          auto now = new cacheUpdateContext;
          *now = *up;
          // now is used immediately, set to true
          now->waiting = true;
          updateCacheline(tick, now);

          ftlreq.beginAt = tick;
          c->ftllist.insert({ftlreq.ftlSubID, {ftlreq, up}});

          debugprint(LOG_ICL_GENERIC_CACHE, "READ  | Cache miss at (%u, %u)",
                      set, way);
          finishTick = MAX(finishTick, tick);
        }
      }
      // AYDTODO: ftlReqId, change it in new code
      // SY: do evict in another place
      //evictCache(&c->req, c->ftlReqId, finishTick, true);
      c->req.finishAt = MAX(c->req.finishAt, finishTick);
    }
    return c->to_ftl;
    });
    context.steps.push_back(REQFUNC {
      if (c->is_sequential) {
        if (c->hit) {
          debugprint(LOG_ICL_GENERIC_CACHE, "READ  | Prefetch done");
        } else {
          debugprint(LOG_ICL_GENERIC_CACHE, "READ  | Read ahead done");
        }
      }
      uint64_t tick = beginAt;
      applyLatency(CPU::ICL__GENERIC_CACHE, CPU::READ, tick);
      c->req.finishAt = MAX(c->req.finishAt, tick);
      return false;
    });
  // useReadCaching == true
  // clean up works
  context.steps.push_back(REQFUNCU {
    stat.request[0]++;
    if (c->hit) stat.cache[0]++;
    return false;
  });
  return context.hit;
}

void GenericCache::write(Request &req, uint64_t &tick){

  if(useWriteCaching){
    //uint64_t flash = tick;
    //bool dirty = false;


    FTL::Request reqInternal(lineCountInSuperPage, mappingUnitInLine, req);

    //SYTODO: write through policy (<=)
   /*if (req.length <= lineSize * mappingUnitInLine ||
        writePolicy == POLICY_WRITE_BACK) {
      dirty = true;
    }
    else {
      pFTL->write(reqInternal, flash);
    }*/

    uint64_t length = req.length;
    uint64_t offset = req.offset;
    uint64_t reqRemain = req.length;
    uint64_t beginLine = req.offset / lineSize;
    uint64_t nLine = (req.offset + req.length + lineSize - 1) / lineSize;
    uint64_t finishedAt = tick;
    uint64_t beginAt;

    req.offset = req.offset % lineSize;
    for(uint64_t i = beginLine; i < nLine; i++){
      beginAt = tick;
      req.subIdx = i;
      req.length = MIN(reqRemain, lineSize - req.offset);
      req.reqSubID++;
      writeInternal(req, beginAt, false);
      req.offset = 0;
      reqRemain -= req.length;
      finishedAt = MAX(beginAt, finishedAt);
    }

    req.length = length;
    req.offset = offset;
  }
  else {
    req.reqSubID++;
    auto key = ReqKey(req);
    submitQueue.insert({key, cacheReqContext()});
    auto &context = submitQueue.at(key);
    context.ftlReqId = 0;
    context.req = req;
    if (context.req.finishAt < context.req.beginAt)
      context.req.finishAt = context.req.beginAt;
    context.arrived = tick;

    debugprint(LOG_ICL_GENERIC_CACHE,
              "WRITE | REQ %7u-%-4u | LCA %" PRIu64 " | SIZE %" PRIu64,
              req.reqID, req.reqSubID, req.range.slpn, req.length);
    context.steps.push_back(REQFUNC {
      FTL::Request ftlReq(lineCountInSuperPage, mappingUnitInLine, c->req);
      ftlReq.beginAt = beginAt;
      ftlReq.ftlSubID = c->ftlReqId++;
      c->ftllist.insert({ftlReq.ftlSubID, {ftlReq, nullptr}});
      return true;
    });
    context.steps.push_back(REQFUNC {
      // AYDTODO: necessary?
      // pDRAM->setScheduling(false);
      uint64_t tick = beginAt;
      pDRAM->read(nullptr, c->req.length, tick);
      // pDRAM->setScheduling(true);
      c->req.finishAt = tick;
      return false;
    });
    // clean up works
    context.steps.push_back(REQFUNCU {
      stat.request[1]++;
      c->hit = false;
      if (c->hit) stat.cache[0]++;
      return false;
    });
  }

}

bool GenericCache::writeInternal(Request &req, uint64_t &tick, bool fromRead = false) {
  auto key = ReqKey(req);
  submitQueue.insert({key, cacheReqContext()});
  auto &context = submitQueue.at(key);
  context.ftlReqId = 0;
  context.req = req;
  context.fromRead = fromRead;
  if (context.req.finishAt < context.req.beginAt)
    context.req.finishAt = context.req.beginAt;
  context.arrived = tick;

  debugprint(LOG_ICL_GENERIC_CACHE,
             "WRITE | REQ %7u-%-4u | LCA %" PRIu64 " | SUBPAGE %u | SIZE %" PRIu64,
             req.reqID, req.reqSubID, req.range.slpn, req.subIdx, req.length);

  // check cache hit
  context.setIdx = calcSetIndex(req.range.slpn);
  context.wayIdx = getValidWay(req.range.slpn, req.subIdx, tick);
  context.hit = context.wayIdx != waySize;
  context.to_ftl = false;
  context.req.finishAt = tick;

  // AYDTODO: async ftl & DRAM
  context.steps.push_front(REQFUNC {
    // SY: move the part
    uint64_t tick = beginAt;
    if (!c->hit) {
      c->wayIdx = getEmptyWay(c->setIdx, tick);
      if (c->wayIdx == waySize) {
        c->to_ftl = true;
        uint32_t setToFlush = c->setIdx;
        if(caculateEvictLine(setToFlush, tick)){
          evictCache(&c->req, c, c->ftlReqId, tick, true);
        }
        c->wayIdx = getEmptyWay(c->setIdx, tick);
      }
      c->req.finishAt = tick;
      if (c->wayIdx == waySize) {
        panic("Cache corrupted!");
      }
    }
    uint32_t s = c->setIdx;
    uint32_t w = c->wayIdx;
    pDRAM->write(&cacheData[s][w], c->req.length, tick);

    bool dirty = c->req.length < lineSize || writePolicy == POLICY_WRITE_BACK;

    auto up = new cacheUpdateContext;
    up->dirty = true, up->valid = true;
    // write will not cause waiting
    up->waiting = false;
    up->tag = up->lca = c->req.range.slpn;
    up->pLine = &cacheData[s][w];
    up->subIdx = c->req.subIdx;
    up->set = s, up->way = w;

    // AYDNOTICE: deep copy and force update at current tick,
    // for we do not care about flash.
    auto now = new cacheUpdateContext;
    *now = *up;

    if(c->fromRead){
      now->dirty = false;
    }
    if (!dirty) {
      FTL::Request ftlReq(lineCountInSuperPage, mappingUnitInLine, c->req);
      ftlReq.beginAt = beginAt;
      ftlReq.ftlSubID = c->ftlReqId++;
      if (c->hit) {
        // When hit, do not wait flash.
        uint64_t _ = tick;
        pFTL->write(ftlReq, _);
      } else {
        c->ftllist.insert({ftlReq.ftlSubID, {ftlReq, up}});
      }
    }
    updateCacheline(tick, now);

    if (c->hit) {
      debugprint(LOG_ICL_GENERIC_CACHE,
                 "WRITE | Cache hit at (%u, %u) | %" PRIu64 " - %" PRIu64
                 " (%" PRIu64 ")",
                 c->setIdx, c->wayIdx, c->arrived, tick, tick - c->arrived);
    } else {
      debugprint(LOG_ICL_GENERIC_CACHE, "WRITE | Cache miss at (%u, %u)",
                 c->setIdx, c->wayIdx);
    }

    c->req.finishAt = tick;
    return !c->ftllist.empty();
  });
  context.steps.push_back(REQFUNC {
    uint64_t tick = beginAt;
    applyLatency(CPU::ICL__GENERIC_CACHE, CPU::WRITE, tick);
    c->req.finishAt = MAX(c->req.finishAt, tick);
    return false;
  }); 
  // clean up works
  context.steps.push_back(REQFUNCU {
    stat.request[1]++;
    if (c->hit) stat.cache[1]++;
    return false;
  });
  return context.hit;
}

void GenericCache::flush(Request &req, uint64_t &tick) {
  auto key = ReqKey(req);
  submitQueue.insert({key, cacheReqContext()});
  auto &context = submitQueue.at(key);
  context.req = req;
  if (context.req.finishAt < context.req.beginAt)
    context.req.finishAt = context.req.beginAt;
  context.arrived = tick;

  debugprint(LOG_ICL_GENERIC_CACHE,
             "FLUSH | REQ %7u-%-4u | LCA %" PRIu64 " | SIZE %" PRIu64,
             req.reqID, req.reqSubID, req.range.slpn, req.length);

  if (useReadCaching || useWriteCaching) {
    context.steps.push_back(REQFUNC {
      uint64_t finishedAt = beginAt;
      uint64_t tick = beginAt;
      bool sent = false;
      auto range = c->req.range;
      FTL::Request reqInternal(c->req);
      reqInternal.reqInfo.clear();
      for (uint32_t setIdx = 0; setIdx < setSize; setIdx++) {
        for (uint32_t wayIdx = 0; wayIdx < waySize; wayIdx++) {
          Line &line = cacheData[setIdx][wayIdx];

          tick += getCacheLatency() * 8;

          if (line.tag >= range.slpn && line.tag < range.slpn + range.nlp) {
            if (line.dirty) {
              uint64_t lpn = line.tag / lineCountInSuperPage;
              uint32_t idx = line.tag % lineCountInSuperPage;

              sent = true;
              reqInternal.beginAt = tick;
              reqInternal.reqType = WRITE;
              reqInternal.ftlSubID = c->ftlReqId++;
              reqInternal.reqInfo.push_back(FTL::ReqInfo(lpn, idx, line.subIdx));
            }
            line.valid = false;
          }
        }
      }
      c->ftllist.insert({reqInternal.ftlSubID, {reqInternal, nullptr}});
      finishedAt = MAX(finishedAt, tick);
      c->req.finishAt = finishedAt;
      return sent;
    });
  }
  context.steps.push_back(REQFUNC {
    applyLatency(CPU::ICL__GENERIC_CACHE, CPU::FLUSH, beginAt);
    c->req.finishAt = beginAt;
    return false;
  });
}

void GenericCache::trim(Request &req, uint64_t &tick) {
  auto key = ReqKey(req);
  submitQueue.insert({key, cacheReqContext()});
  auto &context = submitQueue.at(key);
  context.req = req;
  if (context.req.finishAt < context.req.beginAt)
    context.req.finishAt = context.req.beginAt;
  context.arrived = tick;

  debugprint(LOG_ICL_GENERIC_CACHE,
             "TRIM | REQ %7u-%-4u | LCA %" PRIu64 " | SIZE %" PRIu64, req.reqID,
             req.reqSubID, req.range.slpn, req.length);

  if (useReadCaching || useWriteCaching) {
    context.steps.push_back(REQFUNC {
      uint64_t finishedAt = beginAt;
      uint64_t tick = beginAt;
      bool sent = false;
      auto range = c->req.range;
      FTL::Request reqInternal(c->req);
      reqInternal.reqInfo.push_back(FTL::ReqInfo(0, 0, 0));
      for (uint32_t setIdx = 0; setIdx < setSize; setIdx++) {
        for (uint32_t wayIdx = 0; wayIdx < waySize; wayIdx++) {
          Line &line = cacheData[setIdx][wayIdx];

          tick += getCacheLatency() * 8;

          if (line.tag >= range.slpn && line.tag < range.slpn + range.nlp) {
            if (line.dirty) {

              reqInternal.reqInfo[0].lpn = line.tag / lineCountInSuperPage;
              reqInternal.reqInfo[0].idx = line.tag % lineCountInSuperPage;
              reqInternal.reqInfo[0].subIdx = line.subIdx;

              // AYDTODO: do not wait FTL trim, because
              // FTL trim only delete mapping, no PAL req will be sent.
              // Thus, wait for FTL trim will cause double-update bug!
              // FTL trim do sync way like format.
              sent = false;
              reqInternal.beginAt = tick;
              reqInternal.reqType = TRIM;
              reqInternal.ftlSubID = c->ftlReqId++;
              pFTL->trim(reqInternal, finishedAt);
              // AYDTODO: SYNC FTL TRIM!
              // c->ftllist.insert({reqInternal.ftlSubID, {reqInternal,
              // nullptr}});
              finishedAt = MAX(finishedAt, tick);
            }
            line.valid = false;
          }
        }
      }
      c->req.finishAt = finishedAt;
      return sent;
    });
  }
  context.steps.push_back(REQFUNC {
    applyLatency(CPU::ICL__GENERIC_CACHE, CPU::TRIM, beginAt);
    c->req.finishAt = beginAt;
    return false;
  });
}

void GenericCache::format(Request &req, uint64_t &tick) {
  auto key = ReqKey(req);
  submitQueue.insert({key, cacheReqContext()});
  auto &context = submitQueue.at(key);
  context.req = req;
  if (context.req.finishAt < context.req.beginAt)
    context.req.finishAt = context.req.beginAt;
  context.arrived = tick;

  debugprint(LOG_ICL_GENERIC_CACHE,
             "FORMAT | REQ %7u-%-4u | LCA %" PRIu64 " | SIZE %" PRIu64,
             req.reqID, req.reqSubID, req.range.slpn, req.length);

  LPNRange range = req.range;
  if (useReadCaching || useWriteCaching) {
    uint64_t lpn;
    uint32_t setIdx;
    uint32_t wayIdx;

    for (uint64_t i = 0; i < range.nlp; i++) {
      for(uint32_t j = 0; j < mappingUnitInLine; j++){
        lpn = range.slpn + i;
        setIdx = calcSetIndex(lpn);
        wayIdx = getValidWay(lpn, j, tick);

        if (wayIdx != waySize) {
          // Invalidate
          cacheData[setIdx][wayIdx].valid = false;
        }
      }
    }
  }

  // Convert unit
  range.slpn /= lineCountInSuperPage;
  range.nlp = (range.nlp - 1) / lineCountInSuperPage + 1;

  pFTL->format(range, tick);
  context.steps.push_back(REQFUNC {
    applyLatency(CPU::ICL__GENERIC_CACHE, CPU::FORMAT, beginAt);
    c->req.finishAt = beginAt;
    return false;
  });

  // TODO:
  // FORMAT currently is sync way
  context.req.finishAt = tick;
  commitCallback();
}
#endif

void GenericCache::getStatList(std::vector<Stats> &list, std::string prefix) {
  Stats temp;

  temp.name = prefix + "generic_cache.read.request_count";
  temp.desc = "Read request count";
  list.push_back(temp);

  temp.name = prefix + "generic_cache.read.from_cache";
  temp.desc = "Read requests that served from cache";
  list.push_back(temp);

  temp.name = prefix + "generic_cache.write.request_count";
  temp.desc = "Write request count";
  list.push_back(temp);

  temp.name = prefix + "generic_cache.write.to_cache";
  temp.desc = "Write requests that served to cache";
  list.push_back(temp);
}

void GenericCache::getStatValues(std::vector<double> &values) {
  values.push_back(stat.request[0]);
  values.push_back(stat.cache[0]);
  values.push_back(stat.request[1]);
  values.push_back(stat.cache[1]);
}

void GenericCache::resetStatValues() { memset(&stat, 0, sizeof(stat)); }

}  // namespace ICL

}  // namespace SimpleSSD
