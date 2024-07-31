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

#include "ftl/page_mapping.hh"

#include <algorithm>
#include <limits>
#include <random>

#include "util/algorithm.hh"
#include "util/bitset.hh"
#include "error/retry.hh"

namespace SimpleSSD {

namespace FTL {

PageMapping::PageMapping(ConfigReader &c, Parameter &p, PAL::PAL *l,
                         DRAM::AbstractDRAM *d)
    : AbstractFTL(p, l, d),
      pPAL(l),
      conf(c),
      lastFreeBlock(param.pageCountToMaxPerf),
      lastFreeBlockFreePage(param.ioUnitInPage),
      bReclaimMore(false) {
  blocks.reserve(param.totalPhysicalBlocks);
  table.reserve(param.totalLogicalBlocks * param.pagesInBlock * 
    param.ioUnitInPage * param.subpageInUnit);

  for (uint32_t i = 0; i < param.totalPhysicalBlocks; i++) {
    freeBlocks.emplace_back(Block(i, param.pagesInBlock, 
      param.ioUnitInPage, param.subpageInUnit));
  }

  nFreeBlocks = param.totalPhysicalBlocks;

  status.totalLogicalPages = param.totalLogicalBlocks * param.pagesInBlock;

  // Allocate free blocks
  for (uint32_t i = 0; i < param.pageCountToMaxPerf; i++) {
    lastFreeBlock.at(i) = getFreeBlock(i);
  }

  lastFreeBlockIndex = 0;

  pPAL->setCommitCallback([this]() { Commit(); });

  memset(&stat, 0, sizeof(stat));

  bRandomTweak = conf.readBoolean(CONFIG_FTL, FTL_USE_RANDOM_IO_TWEAK);
  bitsetSize = bRandomTweak ? param.ioUnitInPage : 1;
  subpageInUnit = bRandomTweak ? param.subpageInUnit : 1;
}

PageMapping::~PageMapping() {}

void PageMapping::updateSubmit() {
  auto iter = receiveQueue.begin();
  while (iter != receiveQueue.end()) {
    auto cur = iter;
    ++iter;
    if (cur->reqType == WRITE) {
      // skip WRITE, we will schedule them
      continue;
    }
    outstandingQueue.push_back(PAL::Request(*cur));
    receiveQueue.erase(cur);
    auto pair = outstandingQueue.end();
    pair--;
    auto &req = *pair;
    auto tick = req.beginAt;
    switch (req.reqType) {
      case READ:
        if (!coalesce_CheckOrInsert(req))
          pPAL->read(req, tick);
        break;
      case WRITE:
        // pPAL->write(req, tick);
        break;
      case PALERASE:
        pPAL->erase(req, tick);
        break;
      default:
        panic("FTL page_mapping meets unexpected PAL request Type %d", 
              req.reqType);
        break;
    }
  }
  // Now we schedule write requests
  bool mp =
      conf.readBoolean(CONFIG_PAL, PAL::PAL_CONFIG::NAND_USE_MULTI_PLANE_OP);
  if (mp) {
    multiplane_Schedule();
  }
  else {
    iter = receiveQueue.begin();
    while (iter != receiveQueue.end()) {
      auto cur = iter;
      ++iter;
      outstandingQueue.push_back(PAL::Request(*cur));
      receiveQueue.erase(cur);
      auto pair = outstandingQueue.end();
      pair--;
      auto &req = *pair;
      auto tick = req.beginAt;
      pPAL->write(req, tick);
    }
  }
  if (!finishedQueue.empty()) commitCallback();
}

void PageMapping::multiplane_Schedule() {
  uint32_t plane = conf.readUint(CONFIG_PAL, PAL::PAL_CONFIG::NAND_PLANE);
  // printf("%lu: mp sched, #plane %u\n", getTick(), plane);
  auto iter = receiveQueue.begin();
  std::list<PAL::Request> batch;
  uint32_t batch_cnt = 0;
  while (iter != receiveQueue.end()) {
    auto cur = iter;
    ++iter;
    uint32_t idx = 0;
    for (idx = 0; idx < cur->ioFlag.size(); ++idx) {
      if (cur->ioFlag.test(idx)) {
        break;
      }
    }
    // printf("%lu:   b %u p %u i %u\n", 
    //       getTick(), cur->blockIndex, cur->pageIndex, idx);
    batch.push_back(PAL::Request(*cur));
    receiveQueue.erase(cur);
    bool is_expected = (idx % plane) == batch_cnt;
    // printf("%lu:   idx mod plane %u, batch_cnt %u\n", 
    //       getTick(), idx % plane, batch_cnt);
    if (is_expected) {
      batch_cnt++;
    }
    if (!is_expected) {
      outstandingQueue.push_back(PAL::Request(batch.front()));
      batch.pop_front();
      auto tosend = outstandingQueue.end();
      tosend--;
      pPAL->write(*tosend, tosend->beginAt);
    }
    if (batch_cnt == plane) {
      auto mpreq = PAL::Request(batch.front());
      mpreq.is_multiplane = true;
      outstandingQueue.push_back(mpreq);
      batch.pop_front();
      auto tosend = outstandingQueue.end();
      tosend--;
      pPAL->write(*tosend, tosend->beginAt);
      while (!batch.empty()) {
        auto r = batch.front();
        uint32_t sidx = 0;
        for (; sidx < r.ioFlag.size(); ++sidx) {
          if (r.ioFlag.test(sidx)) {
            break;
          }
        }
        debugprint(LOG_FTL_PAGE_MAPPING,
                   "PAL req id=%lu-%lu-%lu "
                   "addr=b%u-sp%u-p%lu "
                   "early commit (multi-plane merge)",
                   r.reqID, r.reqSubID, r.ftlSubID, r.blockIndex,
                   r.pageIndex, sidx);
        r.finishAt = r.beginAt;
        finishedQueue.push_back(r);
        batch.pop_front();
      }
      batch_cnt = 0;
    }
  }
  // clear remaining
  while(!batch.empty()) {
    outstandingQueue.push_back(PAL::Request(batch.back()));
    batch.pop_back();
    auto tosend = outstandingQueue.end();
    tosend--;
    pPAL->write(*tosend, tosend->beginAt);
  }
  batch_cnt = 0;

}

void PageMapping::Commit() {
  PAL::Request palreq(0);
  while (pPAL->commitToUpper(palreq)) {
    auto iter = outstandingQueue.begin();
    bool find = false;
    while (iter != outstandingQueue.end()) {
      if (iter->reqID == palreq.reqID && iter->reqSubID == palreq.reqSubID &&
          iter->ftlSubID == palreq.ftlSubID &&
          iter->blockIndex == palreq.blockIndex &&
          iter->pageIndex == palreq.pageIndex &&
          iter->reqType == palreq.reqType) {
        find = true;
        CPU::FUNCTION func = CPU::READ;
        CPU::FUNCTION ifunc = CPU::READ_INTERNAL;
        if (palreq.reqType == WRITE) { 
          func = CPU::WRITE;
          ifunc = CPU::WRITE_INTERNAL;
        }
        if (palreq.reqType == PALERASE) {
          ifunc = CPU::ERASE_INTERNAL;
        }

        applyLatency(CPU::FTL__PAGE_MAPPING, ifunc, palreq.finishAt);
        if (palreq.reqType != PALERASE) {
          applyLatency(CPU::FTL__PAGE_MAPPING, func, palreq.finishAt);
        }
        finishedQueue.push_back(PAL::Request(palreq));
        outstandingQueue.erase(iter);
        coalesce_PALCommit(palreq);
        break;
      }
      ++iter;
    }
    if (find == false) {
      iter = GCQueue.begin();
      while (iter != GCQueue.end()) {
        if (iter->reqID == palreq.reqID && iter->reqSubID == palreq.reqSubID && 
            iter->ftlSubID == palreq.ftlSubID &&
            iter->blockIndex == palreq.blockIndex &&
            iter->pageIndex == palreq.pageIndex &&
            iter->reqType == palreq.reqType) {
          find = true;
          debugprint(LOG_FTL, "Finished a GC request");
          GCQueue.erase(iter);
          break;
        }
        ++iter;
      }
    }
    if (find == false) {
      panic("page_mapping: unexpected PAL request %lu-%lu-%lu block%u-page%u", 
            palreq.reqID, palreq.reqSubID, palreq.ftlSubID, 
            palreq.blockIndex, palreq.pageIndex);
    }
  }
  commitCallback();
}

bool PageMapping::initialize() {
  uint64_t nPagesToWarmup;
  uint64_t nPagesToInvalidate;
  uint64_t nTotalLogicalPages;
  uint64_t maxPagesBeforeGC;
  uint64_t tick;
  uint64_t valid;
  uint64_t invalid;
  FILLING_MODE mode;

  Request req;

  debugprint(LOG_FTL_PAGE_MAPPING, "Initialization started");

  nTotalLogicalPages = param.totalLogicalBlocks * param.pagesInBlock;
  nPagesToWarmup =
      nTotalLogicalPages * conf.readFloat(CONFIG_FTL, FTL_FILL_RATIO);
  nPagesToInvalidate =
      nTotalLogicalPages * conf.readFloat(CONFIG_FTL, FTL_INVALID_PAGE_RATIO);
  mode = (FILLING_MODE)conf.readUint(CONFIG_FTL, FTL_FILLING_MODE);
  maxPagesBeforeGC =
      param.pagesInBlock *
      (param.totalPhysicalBlocks *
           (1 - conf.readFloat(CONFIG_FTL, FTL_GC_THRESHOLD_RATIO)) -
       param.pageCountToMaxPerf);  // # free blocks to maintain

  if (nPagesToWarmup + nPagesToInvalidate > maxPagesBeforeGC) {
    warn("ftl: Too high filling ratio. Adjusting invalidPageRatio.");
    nPagesToInvalidate = maxPagesBeforeGC - nPagesToWarmup;
  }

  debugprint(LOG_FTL_PAGE_MAPPING, "Total logical pages: %" PRIu64,
             nTotalLogicalPages);
  debugprint(LOG_FTL_PAGE_MAPPING,
             "Total logical pages to fill: %" PRIu64 " (%.2f %%)",
             nPagesToWarmup, nPagesToWarmup * 100.f / nTotalLogicalPages);
  debugprint(LOG_FTL_PAGE_MAPPING,
             "Total invalidated pages to create: %" PRIu64 " (%.2f %%)",
             nPagesToInvalidate,
             nPagesToInvalidate * 100.f / nTotalLogicalPages);

  for(uint32_t i = 0; i < bitsetSize * subpageInUnit; i++){
    req.reqInfo.push_back(ReqInfo(0, 
        (i / subpageInUnit) % bitsetSize, i % subpageInUnit));
  }

  // Step 1. Filling
  if (mode == FILLING_MODE_0 || mode == FILLING_MODE_1) {
    // Sequential
    for (uint64_t i = 0; i < nPagesToWarmup; i++) {
      tick = 0;
      req.setLPN(i);
      writeInternal(nullptr, req, tick, false);
    }
  }
  else {
    // Random
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist(0, nTotalLogicalPages - 1);

    for (uint64_t i = 0; i < nPagesToWarmup; i++) {
      tick = 0;
      req.setLPN(dist(gen));
      writeInternal(nullptr, req, tick, false);
    }
  }

  // Step 2. Invalidating
  if (mode == FILLING_MODE_0) {
    // Sequential
    for (uint64_t i = 0; i < nPagesToInvalidate; i++) {
      tick = 0;
      req.setLPN(i);
      writeInternal(nullptr, req, tick, false);
    }
  }
  else if (mode == FILLING_MODE_1) {
    // Random
    // We can successfully restrict range of LPN to create exact number of
    // invalid pages because we wrote in sequential mannor in step 1.
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist(0, nPagesToWarmup - 1);

    for (uint64_t i = 0; i < nPagesToInvalidate; i++) {
      tick = 0;
      req.setLPN(dist(gen));
      writeInternal(nullptr, req, tick, false);
    }
  }
  else {
    // Random
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist(0, nTotalLogicalPages - 1);

    for (uint64_t i = 0; i < nPagesToInvalidate; i++) {
      tick = 0;
      req.setLPN(dist(gen));
      writeInternal(nullptr, req, tick, false);
    }
  }

  // Report
  calculateTotalPages(valid, invalid);
  debugprint(LOG_FTL_PAGE_MAPPING, "Filling finished. Page status:");
  debugprint(LOG_FTL_PAGE_MAPPING,
             "  Total valid physical pages: %" PRIu64
             " (%.2f %%, target: %" PRIu64 ", error: %" PRId64 ")",
             valid, valid * 100.f / nTotalLogicalPages, nPagesToWarmup,
             (int64_t)(valid - nPagesToWarmup));
  debugprint(LOG_FTL_PAGE_MAPPING,
             "  Total invalid physical pages: %" PRIu64
             " (%.2f %%, target: %" PRIu64 ", error: %" PRId64 ")",
             invalid, invalid * 100.f / nTotalLogicalPages, nPagesToInvalidate,
             (int64_t)(invalid - nPagesToInvalidate));
  debugprint(LOG_FTL_PAGE_MAPPING, "Initialization finished");

  return true;
}

bool MappingInfo::operator==(const MappingInfo &iter){
  return this->blockIndex == iter.blockIndex &&
         this->idx == iter.idx &&
         this->pageIndex == iter.pageIndex;
}

bool MappingInfo::operator<(const MappingInfo &iter){
  if(this->blockIndex < iter.blockIndex){
    return true;
  }
  else if(this->blockIndex == iter.blockIndex){
    if(this->pageIndex < iter.pageIndex){
      return true;
    }
    else if(this->idx == iter.idx){
      if(this->idx < iter.idx){
        return true;
      }
    }
  }
  return false;
}

bool MappingInfo::operator>(const MappingInfo &iter){
  if(this->blockIndex > iter.blockIndex){
    return true;
  }
  else if(this->blockIndex == iter.blockIndex){
    if(this->pageIndex > iter.pageIndex){
      return true;
    }
    else if(this->idx == iter.idx){
      if(this->idx > iter.idx){
        return true;
      }
    }
  }
  return false;
}

uint64_t PageMapping::mappingTo64(MappingInfo &mapping){
  uint64_t index = (mapping.blockIndex * param.pagesInBlock 
    + mapping.pageIndex) * bitsetSize + mapping.idx;

  return index;
}

bool PageMapping::coalesce_CheckOrInsert(PAL::Request &read) {
  if (read.reqType != READ) {
    return false;
  }
  MappingInfo info(read);
  uint64_t index = mappingTo64(info);
  auto have = PALReadCoalesceMap.count(index);
  if (have) {
    auto entry = CoalescedReq{read.reqID, read.reqSubID, read.ftlSubID};
    PALReadCoalesceMap.at(index).push_back(entry);
    debugprint(LOG_FTL_PAGE_MAPPING,
               "FTL Read %lu-%lu-%lu coalesced | Block %lu | Idx %lu | PageIndex %lu",
               read.reqID, read.reqSubID, read.ftlSubID,  
               info.blockIndex, info.idx, info.pageIndex);
    return true;
  } else {
    // insert the entry to let other reqs know the origin req
    PALReadCoalesceMap.insert(
        std::make_pair(index, std::vector<CoalescedReq>()));
    debugprint(LOG_FTL_PAGE_MAPPING,
      "New coalescing info recorded: PAL Read | Block %lu | Idx %lu | PageIndex %lu",
      read.reqID, read.reqSubID, read.ftlSubID, 
      info.blockIndex, info.idx, info.pageIndex);
    return false;
  }
}

void PageMapping::coalesce_PALCommit(PAL::Request &commit) {
  if (commit.reqType != READ) {
    return;
  }
  MappingInfo info(commit);
  uint64_t index = mappingTo64(info);
  auto have = PALReadCoalesceMap.count(index);
  if (have) {
    auto &list = PALReadCoalesceMap.at(index);
    for (auto entry : list){
      auto iter = outstandingQueue.begin();
      while (iter != outstandingQueue.end()) {
        if (iter->reqID == entry.reqID && 
            iter->reqSubID == entry.reqSubID &&
            iter->ftlSubID == entry.ftlReqID &&
            iter->blockIndex == commit.blockIndex &&
            iter->pageIndex == commit.pageIndex &&
            iter->reqType == commit.reqType) {
            debugprint(LOG_FTL_PAGE_MAPPING, 
                  "coalesced PAL Read %lu-%lu-%lu finished due to commit of "
                  "%lu-%lu-%lu", 
                  iter->reqID, iter->reqSubID, iter->ftlSubID, 
                  commit.reqID, commit.reqSubID, commit.ftlSubID);
            iter->finishAt = commit.finishAt;
            iter->coalesced = true;
            finishedQueue.push_back(PAL::Request(*iter));
            outstandingQueue.erase(iter);
            break;
        }
        ++iter;
      }
    }
    PALReadCoalesceMap.erase(index);
  }  // else return
}

void PageMapping::read(std::vector<PAL::Request> *pQue, 
                       Request &req, uint64_t &tick) {
  uint64_t begin = tick;

  if (req.reqInfo.size() > 0) {
    readInternal(pQue, req, tick);

    debugprint(LOG_FTL_PAGE_MAPPING,
               "READ  | LPN %" PRIu64 " | %" PRIu64 " - %" PRIu64 " (%" PRIu64
               ")",
               req.reqInfo[0].lpn, begin, tick, tick - begin);
  }
  else {
    warn("FTL got empty request");
  }

  applyLatency(CPU::FTL__PAGE_MAPPING, CPU::READ, tick);
}

void PageMapping::write(std::vector<PAL::Request> *pQue, 
                        Request &req, uint64_t &tick) {
  uint64_t begin = tick;

  if (req.reqInfo.size() > 0) {
    writeInternal(pQue, req, tick);

    debugprint(LOG_FTL_PAGE_MAPPING,
               "WRITE | LPN %" PRIu64 " | %" PRIu64 " - %" PRIu64 " (%" PRIu64
               ")",
               req.reqInfo[0].lpn, begin, tick, tick - begin);
  }
  else {
    warn("FTL got empty request");
  }

  applyLatency(CPU::FTL__PAGE_MAPPING, CPU::WRITE, tick);
}

void PageMapping::trim(std::vector<PAL::Request> *pQue, 
                       Request &req, uint64_t &tick) {
  uint64_t begin = tick;

  trimInternal(pQue, req, tick);

  debugprint(LOG_FTL_PAGE_MAPPING,
             "TRIM  | LPN %" PRIu64 " | %" PRIu64 " - %" PRIu64 " (%" PRIu64
             ")",
             req.reqInfo[0].lpn, begin, tick, tick - begin);

  applyLatency(CPU::FTL__PAGE_MAPPING, CPU::TRIM, tick);
}

void PageMapping::format(LPNRange &range, uint64_t &tick) {
  PAL::Request req(param.ioUnitInPage);
  std::vector<uint32_t> list;

  req.ioFlag.set();

  for (auto iter = table.begin(); iter != table.end();) {
    uint64_t lca = iter->first / subpageInUnit;
    if (lca >= range.slpn && lca < range.slpn + range.nlp) {
      auto &mapping = iter->second;

      // Do trim       
      auto block = blocks.find(mapping.blockIndex);

      if (block == blocks.end()) {
        panic("Block is not in use");
      }

      block->second.invalidate(mapping.pageIndex, mapping.idx, mapping.subIdx);

      // Collect block indices
      list.push_back(mapping.blockIndex);

     iter = table.erase(iter);
    }
    else {
      iter++;
    }
  }

  // Get blocks to erase
  std::sort(list.begin(), list.end());
  auto last = std::unique(list.begin(), list.end());
  list.erase(last, list.end());

  // Do GC only in specified blocks
  doGarbageCollection(list, tick);

  applyLatency(CPU::FTL__PAGE_MAPPING, CPU::FORMAT, tick);
}

Status *PageMapping::getStatus(uint64_t lpnBegin, uint64_t lpnEnd) {
  status.freePhysicalBlocks = nFreeBlocks;

  if (lpnBegin == 0 && lpnEnd >= status.totalLogicalPages) {
    status.mappedLogicalPages = table.size();
  }
  else {
    status.mappedLogicalPages = 0;

    for (uint64_t lpn = lpnBegin; lpn < lpnEnd; lpn++) {
      if (table.count(lpn) > 0) {
        status.mappedLogicalPages++;
      }
    }
  }

  return &status;
}

float PageMapping::freeBlockRatio() {
  return (float)nFreeBlocks / param.totalPhysicalBlocks;
}

uint32_t PageMapping::convertBlockIdx(uint32_t blockIdx) {
  return blockIdx % param.pageCountToMaxPerf;
}

uint32_t PageMapping::getFreeBlock(uint32_t idx) {
  uint32_t blockIndex = 0;

  if (idx >= param.pageCountToMaxPerf) {
    panic("Index out of range");
  }

  if (nFreeBlocks > 0) {
    // Search block which is blockIdx % param.pageCountToMaxPerf == idx
    auto iter = freeBlocks.begin();

    for (; iter != freeBlocks.end(); iter++) {
      blockIndex = iter->getBlockIndex();

      if (blockIndex % param.pageCountToMaxPerf == idx) {
        break;
      }
    }

    // Sanity check
    if (iter == freeBlocks.end()) {
      // Just use first one
      iter = freeBlocks.begin();
      blockIndex = iter->getBlockIndex();
    }

    // Insert found block to block list
    if (blocks.find(blockIndex) != blocks.end()) {
      panic("Corrupted");
    }

    blocks.emplace(blockIndex, std::move(*iter));

    // Remove found block from free block list
    freeBlocks.erase(iter);
    nFreeBlocks--;
  }
  else {
    panic("No free block left");
  }

  return blockIndex;
}

uint32_t PageMapping::getLastFreeBlock(uint32_t nPage) {
  if (!bRandomTweak || lastFreeBlockFreePage < nPage) {
    // Update lastFreeBlockIndex
    lastFreeBlockIndex++;

    if (lastFreeBlockIndex == param.pageCountToMaxPerf) {
      lastFreeBlockIndex = 0;
    }

    lastFreeBlockFreePage = bitsetSize - nPage;
  }
  else {
    lastFreeBlockFreePage -= nPage;
  }

  auto freeBlock = blocks.find(lastFreeBlock.at(lastFreeBlockIndex));

  // Sanity check
  if (freeBlock == blocks.end()) {
    panic("Corrupted");
  }
  // If current free block is full, get next block
  if (freeBlock->second.getFreePage() < nPage) {
    lastFreeBlock.at(lastFreeBlockIndex) = getFreeBlock(lastFreeBlockIndex);
    bReclaimMore = true;
  }

  return lastFreeBlock.at(lastFreeBlockIndex);
}

// calculate weight of each block regarding victim selection policy
void PageMapping::calculateVictimWeight(
    std::vector<std::pair<uint32_t, float>> &weight, const EVICT_POLICY policy,
    uint64_t tick) {
  float temp;

  weight.reserve(blocks.size());

  switch (policy) {
    case POLICY_GREEDY:
    case POLICY_RANDOM:
    case POLICY_DCHOICE:
      for (auto &iter : blocks) {
        if (iter.second.getNextWritePageIndex() != param.pagesInBlock) {
          continue;
        }

        weight.push_back({iter.first, iter.second.getValidPageCountRaw()});
      }

      break;
    case POLICY_COST_BENEFIT:
      for (auto &iter : blocks) {
        if (iter.second.getNextWritePageIndex() != param.pagesInBlock) {
          continue;
        }

        temp = (float)(iter.second.getValidPageCountRaw()) / param.pagesInBlock;

        weight.push_back(
            {iter.first,
             temp / ((1 - temp) * (tick - iter.second.getLastAccessedTime()))});
      }

      break;
    default:
      panic("Invalid evict policy");
  }
}

void PageMapping::selectVictimBlock(std::vector<uint32_t> &list,
                                    uint64_t &tick) {
  static const GC_MODE mode = (GC_MODE)conf.readInt(CONFIG_FTL, FTL_GC_MODE);
  static const EVICT_POLICY policy =
      (EVICT_POLICY)conf.readInt(CONFIG_FTL, FTL_GC_EVICT_POLICY);
  static uint32_t dChoiceParam =
      conf.readUint(CONFIG_FTL, FTL_GC_D_CHOICE_PARAM);
  uint64_t nBlocks = conf.readUint(CONFIG_FTL, FTL_GC_RECLAIM_BLOCK);
  std::vector<std::pair<uint32_t, float>> weight;

  list.clear();

  // Calculate number of blocks to reclaim
  if (mode == GC_MODE_0) {
    // DO NOTHING
  }
  else if (mode == GC_MODE_1) {
    static const float t = conf.readFloat(CONFIG_FTL, FTL_GC_RECLAIM_THRESHOLD);

    nBlocks = param.totalPhysicalBlocks * t - nFreeBlocks;
  }
  else {
    panic("Invalid GC mode");
  }

  // reclaim one more if last free block fully used
  if (bReclaimMore) {
    nBlocks += param.pageCountToMaxPerf;

    bReclaimMore = false;
  }

  // Calculate weights of all blocks
  calculateVictimWeight(weight, policy, tick);

  if (policy == POLICY_RANDOM || policy == POLICY_DCHOICE) {
    uint64_t randomRange =
        policy == POLICY_RANDOM ? nBlocks : dChoiceParam * nBlocks;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint64_t> dist(0, weight.size() - 1);
    std::vector<std::pair<uint32_t, float>> selected;

    while (selected.size() < randomRange) {
      uint64_t idx = dist(gen);

      if (weight.at(idx).first < std::numeric_limits<uint32_t>::max()) {
        selected.push_back(weight.at(idx));
        weight.at(idx).first = std::numeric_limits<uint32_t>::max();
      }
    }

    weight = std::move(selected);
  }

  // Sort weights
  std::sort(
      weight.begin(), weight.end(),
      [](std::pair<uint32_t, float> a, std::pair<uint32_t, float> b) -> bool {
        return a.second < b.second;
      });

  // Select victims from the blocks with the lowest weight
  nBlocks = MIN(nBlocks, weight.size());

  for (uint64_t i = 0; i < nBlocks; i++) {
    list.push_back(weight.at(i).first);
  }

  applyLatency(CPU::FTL__PAGE_MAPPING, CPU::SELECT_VICTIM_BLOCK, tick);
}

void PageMapping::doGarbageCollection(std::vector<uint32_t> &blocksToReclaim,
                                      uint64_t &tick) {
  PAL::Request req(param.ioUnitInPage);
  std::vector<PAL::Request> readRequests;
  std::vector<PAL::Request> writeRequests;
  std::vector<PAL::Request> eraseRequests;
  std::vector<std::vector<ReqInfo>> lpns;
  Bitset bit(param.ioUnitInPage);
  uint64_t beginAt;
  uint64_t readFinishedAt = tick;
  uint64_t writeFinishedAt = tick;
  uint64_t eraseFinishedAt = tick;

  if (blocksToReclaim.size() == 0) {
    return;
  }
  // For all blocks to reclaim, collecting request structure only
  for (auto &iter : blocksToReclaim) {
    auto block = blocks.find(iter);
    if (block == blocks.end()) {
      panic("Invalid block");
    }

    // Copy valid pages to free block
    for (uint32_t pageIndex = 0; pageIndex < param.pagesInBlock; pageIndex++) {
      // Valid?
      lpns.clear();
      if (block->second.getPageInfo(pageIndex, lpns, bit)) {
        if (!bRandomTweak) {
          bit.set();
        }
        // Retrive free block
        uint32_t needPages = block->second.getValidMappingUnit(pageIndex);
        needPages = (needPages + subpageInUnit - 1) / subpageInUnit;
        auto freeBlock = blocks.find(getLastFreeBlock(needPages));

        // Issue Read
        req.blockIndex = block->first;
        req.pageIndex = pageIndex;
        req.ioFlag = bit;
        req.is_gc = true;

        readRequests.push_back(req);
        // Update mapping table
        uint32_t newBlockIdx = freeBlock->first;
        uint32_t newPageIdx = freeBlock->second.getNextWritePageIndex();
        uint32_t newUnitIdx = freeBlock->second.getNextWriteUnitIndex();

        std::vector<ReqInfo> reMapList;
        reMapList.resize(subpageInUnit);
        uint32_t nSubPage = 0;

        for (uint32_t idx = 0; idx < bitsetSize; idx++) {
          if (bit.test(idx)) {
            // Invalidate
            for(uint32_t j = 0; j < subpageInUnit; j++){
              if(lpns[idx][j].subIdx < subpageInUnit || !bRandomTweak){
                block->second.invalidate(pageIndex, idx, j);
                ReqInfo reMap = lpns[idx][j];
                auto mappingList = table.find(getTableIndex(reMap));

                if (mappingList == table.end()) {
                  panic("Invalid mapping table entry");
                }

                auto &mapping = mappingList->second;
                mapping.blockIndex = newBlockIdx;
                mapping.pageIndex = newPageIdx;
                mapping.idx = newUnitIdx;
                mapping.subIdx = nSubPage;

                reMapList[nSubPage] = reMap;
                nSubPage++;

                if(nSubPage == subpageInUnit){
                  //debugprint(LOG_FTL_PAGE_MAPPING, "Write %u %u %u", freeBlock->first, newPageIdx, newUnitIdx);
                  freeBlock->second.write(newPageIdx, reMapList, newUnitIdx, beginAt, bRandomTweak);

                  // Issue Write
                  req.blockIndex = newBlockIdx;
                  req.pageIndex = newPageIdx;
                  req.is_gc = true;

                  if (bRandomTweak) {
                    req.ioFlag.reset();
                    req.ioFlag.set(newUnitIdx);
                  }
                  else {
                    req.ioFlag.set();
                  }

                  writeRequests.push_back(req);

                  stat.validPageCopies++;

                  nSubPage = 0;
                  newPageIdx = freeBlock->second.getNextWritePageIndex();
                  newUnitIdx = freeBlock->second.getNextWriteUnitIndex();
                }

              }
            }
          }
        }
        if(nSubPage > 0){
          ReqInfo copy;
          copy.subIdx = subpageInUnit;
          for(uint32_t i = nSubPage; i < subpageInUnit; i++){
            reMapList[i] = copy;
          }
          freeBlock->second.write(newPageIdx, reMapList, newUnitIdx, beginAt, bRandomTweak);
          req.blockIndex = newBlockIdx;
          req.pageIndex = newPageIdx;
          req.is_gc = true;

          if (bRandomTweak) {
            req.ioFlag.reset();
            req.ioFlag.set(newUnitIdx);
          }
          else {
            req.ioFlag.set();
          }

          writeRequests.push_back(req);

          stat.validPageCopies++;
        }

        stat.validSuperPageCopies++;
      }
    }

    // Erase block
    req.blockIndex = block->first;
    req.pageIndex = 0;
    req.ioFlag.set();
    req.is_gc = true;

    eraseRequests.push_back(req);
  }

  // Do actual I/O here
  // This handles PAL2 limitation (SIGSEGV, infinite loop, or so-on)
  for (auto &iter : readRequests) {
    pDRAM->read(nullptr, 8 * param.ioUnitInPage, tick);

    beginAt = tick;

    iter.reqType = READ;
    PAL::Request palreq(iter);
    GCQueue.push_back(palreq);
    pPAL->read(iter, beginAt);

    pDRAM->write(nullptr, param.pageSize, beginAt);
    applyLatency(CPU::ECC, CPU::ECC_DECODE, beginAt);
    readFinishedAt = MAX(readFinishedAt, beginAt);
  }

  for (auto &iter : writeRequests) {
    pDRAM->write(nullptr, 8, tick);

    beginAt = readFinishedAt;

    iter.reqType = WRITE;
    PAL::Request palreq(iter);
    GCQueue.push_back(palreq);
    pPAL->write(iter, beginAt);

    pDRAM->read(nullptr, param.pageSize / param.ioUnitInPage, beginAt);
    writeFinishedAt = MAX(writeFinishedAt, beginAt);
  }

  for (auto &iter : eraseRequests) {
    beginAt = readFinishedAt;

    iter.reqType = PALERASE;
    PAL::Request palreq(iter);
    GCQueue.push_back(palreq);
    eraseInternal(iter, beginAt, true);

    eraseFinishedAt = MAX(eraseFinishedAt, beginAt);
  }

  tick = MAX(writeFinishedAt, eraseFinishedAt);
  applyLatency(CPU::FTL__PAGE_MAPPING, CPU::DO_GARBAGE_COLLECTION, tick);
}

uint64_t PageMapping::getTableIndex(ReqInfo &info){
  return (info.lpn * bitsetSize + info.idx) * subpageInUnit + info.subIdx;
}

void PageMapping::readInternal(std::vector<PAL::Request> *pQue, 
                               Request &req, uint64_t &tick) {

  PAL::Request palRequest(param.ioUnitInPage, req);
  uint64_t beginAt;
  uint64_t finishedAt = tick;

  std::vector<MappingInfo> readList;
  uint32_t readNum = req.reqInfo.size();

  for(uint32_t i = 0; i < readNum; i++){
    auto mappingList = table.find(getTableIndex(req.reqInfo[i]));
    if (mappingList != table.end()) {
      if (bRandomTweak) {
        pDRAM->read(&(*mappingList), 8, tick);
      }
      else {
        pDRAM->read(&(*mappingList), 8, tick);
      }
      auto &mapping = mappingList->second;
      if(mapping.blockIndex < param.totalPhysicalBlocks &&
           mapping.pageIndex < param.pagesInBlock){

        readList.push_back(mapping); 
      }
    }
  }

  //Remove duplicate elements
  std::sort(readList.begin(), readList.end());
  readList.erase(std::unique(readList.begin(), readList.end()), readList.end());

  if(readList.size() == 0){
    debugprint(LOG_FTL_PAGE_MAPPING, "Read an invalid page");
    req.readValidPage = false;
  }
  else{
    req.readValidPage = true;
  }

  for (auto &iter : readList)  {
    palRequest.blockIndex = iter.blockIndex;
    palRequest.pageIndex = iter.pageIndex;
    if(bRandomTweak){
      palRequest.ioFlag.reset();
      palRequest.ioFlag.set(iter.idx);
    }
    else{
      palRequest.ioFlag.set();
    }
    auto block = blocks.find(palRequest.blockIndex);
    if(block == blocks.end()){
      panic("Block is not in use");
    }
    beginAt = tick;

    block->second.read(iter.pageIndex, iter.idx, beginAt, req.backInfo);
    palRequest.reqType = READ;
    palRequest.beginAt = beginAt;
    if (pQue != nullptr) pQue->push_back(PAL::Request(palRequest));
    receiveQueue.push_back(PAL::Request(palRequest));
    // if(!coalesce_CheckOrInsert(palRequest)){
    //   pPAL->read(palRequest, beginAt);
    // }

    finishedAt = MAX(finishedAt, beginAt);
    applyLatency(CPU::ECC, CPU::ECC_DECODE, finishedAt);
  }

  tick = finishedAt;
  applyLatency(CPU::FTL__PAGE_MAPPING, CPU::READ_INTERNAL, tick);
}

void PageMapping::writeInternal(std::vector<PAL::Request> *pQue, 
                                Request &req, uint64_t &tick, bool sendToPAL) {
  PAL::Request palRequest(param.ioUnitInPage, req);
  std::unordered_map<uint32_t, Block>::iterator block;
  uint32_t writeNum = req.reqInfo.size();
  uint64_t beginAt;
  uint64_t finishedAt = tick;

  for(uint32_t i = 0; i < writeNum; i++){
    if(req.reqInfo[i].subIdx == subpageInUnit){
      continue;
    }
    auto mappingList = table.find(getTableIndex(req.reqInfo[i]));
    if(mappingList != table.end()){
      auto &mapping = mappingList->second;
      if (mapping.blockIndex < param.totalPhysicalBlocks &&
                  mapping.pageIndex < param.pagesInBlock) {
        
        block = blocks.find(mapping.blockIndex);
        block->second.invalidate(mapping.pageIndex, mapping.idx, mapping.subIdx);
      }
    }
    else {
      // Create empty mapping
      MappingInfo copy;
      copy.blockIndex = param.totalPhysicalBlocks;
      copy.pageIndex = param.pagesInBlock;
      auto ret = table.emplace(
          getTableIndex(req.reqInfo[i]), copy);

      if (!ret.second) {
        panic("Failed to insert new mapping");
      }

      mappingList = ret.first;
    }

  }

  // Write data to free block
  block = blocks.find(getLastFreeBlock(
      (writeNum + subpageInUnit - 1) / subpageInUnit));

  if (block == blocks.end()) {
    panic("No such block");
  }

  if (sendToPAL) {
    if (bRandomTweak) {
      // 4 * nlp mapping unit
      // 
      pDRAM->read(nullptr, writeNum * 8, tick);
      pDRAM->write(nullptr, writeNum * 8, tick);
    }
    else {
      pDRAM->read(nullptr, 8, tick);
      pDRAM->write(nullptr, 8, tick);
    }
  }

  //SYTODO: check readbeforewrite
  /*if (!bRandomTweak && !req.ioFlag.all()) {
    // We have to read old data
    readBeforeWrite = true;
  }*/
  std::vector<ReqInfo> reMap;
  reMap.resize(subpageInUnit);
  uint32_t nSubPage = 0;
  uint32_t pageIndex = block->second.getNextWritePageIndex();
  uint32_t unitIndex = block->second.getNextWriteUnitIndex();

  for(uint32_t i = 0; i < writeNum; i++){
    if(sendToPAL) {
      applyLatency(CPU::ECC, CPU::ECC_ENCODE, tick);
    }

    if(req.reqInfo[i].subIdx < subpageInUnit){
      auto mappingList = table.find(getTableIndex(req.reqInfo[i]));
      auto &mapping = mappingList->second;

      // update mapping to table
      mapping.blockIndex = block->first;
      mapping.pageIndex = pageIndex;
      mapping.idx = unitIndex;
      mapping.subIdx = nSubPage;
    }
    reMap[nSubPage] = req.reqInfo[i];
    nSubPage++;


    if(nSubPage == subpageInUnit){
      nSubPage = 0;

      beginAt = tick;
      block->second.write(pageIndex, reMap, unitIndex, beginAt, bRandomTweak);

              
      if (sendToPAL) {
        palRequest.blockIndex = block->first;
        palRequest.pageIndex = pageIndex;

        if (bRandomTweak) {
          palRequest.ioFlag.reset();
          palRequest.ioFlag.set(unitIndex);
        }
        else {
          palRequest.ioFlag.set();
        }

        palRequest.reqType = WRITE;
        palRequest.beginAt = beginAt;
        if (pQue != nullptr) pQue->push_back(PAL::Request(palRequest));
        receiveQueue.push_back(PAL::Request(palRequest));
        // pPAL->write(palRequest, beginAt);
      }
      finishedAt = MAX(finishedAt, beginAt);
      pageIndex = block->second.getNextWritePageIndex();
      unitIndex = block->second.getNextWriteUnitIndex();
    }
  }

  if(nSubPage > 0){
    beginAt = tick;
    ReqInfo copy;
    copy.subIdx = subpageInUnit;
    for(uint32_t i = nSubPage; i < subpageInUnit; i++){
      reMap[i] = copy;
    }
    block->second.write(pageIndex, reMap, unitIndex, beginAt, bRandomTweak);
    if (sendToPAL) {
      palRequest.blockIndex = block->first;
      palRequest.pageIndex = pageIndex;

      if (bRandomTweak) {
        palRequest.ioFlag.reset();
        palRequest.ioFlag.set(unitIndex);
      }
      else {
        palRequest.ioFlag.set();
      }
      palRequest.reqType = WRITE;
      palRequest.beginAt = beginAt;
      if (pQue != nullptr) pQue->push_back(PAL::Request(palRequest));
      receiveQueue.push_back(PAL::Request(palRequest));
      // pPAL->write(palRequest, beginAt);
      finishedAt = MAX(finishedAt, beginAt);
    }
  }


  // Exclude CPU operation when initializing
  if (sendToPAL) {
    tick = finishedAt;
    applyLatency(CPU::FTL__PAGE_MAPPING, CPU::WRITE_INTERNAL, tick);
  }

  // GC if needed
  // I assumed that init procedure never invokes GC
  static float gcThreshold = conf.readFloat(CONFIG_FTL, FTL_GC_THRESHOLD_RATIO);

  if (freeBlockRatio() < gcThreshold) {
    if (!sendToPAL) {
      panic("ftl: GC triggered while in initialization");
    }

    std::vector<uint32_t> list;
    uint64_t beginAt = tick;

    selectVictimBlock(list, beginAt);

    debugprint(LOG_FTL_PAGE_MAPPING,
               "GC   | On-demand | %u blocks will be reclaimed", list.size());

    doGarbageCollection(list, beginAt);

    debugprint(LOG_FTL_PAGE_MAPPING,
               "GC   | Done | %" PRIu64 " - %" PRIu64 " (%" PRIu64 ")", tick,
               beginAt, beginAt - tick);

    stat.gcCount++;
    stat.reclaimedBlocks += list.size();
  }
}

void PageMapping::trimInternal(std::vector<PAL::Request> *, 
                               Request &req, uint64_t &tick) {
  bool doTrim = false;
  uint32_t trimNum = req.reqInfo.size();
  for(uint32_t i = 0; i < trimNum; i++){
    auto mappingList = table.find(getTableIndex(req.reqInfo[i]));
    if(mappingList != table.end()){
      doTrim = true;
      if (bRandomTweak) {
        pDRAM->read(&(*mappingList), 8, tick);
      }
      else {
        pDRAM->read(&(*mappingList), 8, tick);
      }
      auto &mapping = mappingList->second;
      auto block = blocks.find(mapping.blockIndex);

      if (block == blocks.end()) {
        panic("Block is not in use");
      }

      block->second.invalidate(mapping.pageIndex, mapping.idx, mapping.subIdx);
    }
    table.erase(mappingList);
  }
  
  if(doTrim){
    applyLatency(CPU::FTL__PAGE_MAPPING, CPU::TRIM_INTERNAL, tick);
  }

}

void PageMapping::eraseInternal(PAL::Request &req, uint64_t &tick, bool isGC) {
  static uint64_t threshold =
      conf.readUint(CONFIG_FTL, FTL_BAD_BLOCK_THRESHOLD);
  auto block = blocks.find(req.blockIndex);

  // Sanity checks
  if (block == blocks.end()) {
    panic("No such block");
  }

  if (block->second.getValidPageCount() != 0) {
    panic("There are valid pages in victim block");
  }

  // Erase block
  block->second.erase();
 
  // AYDTODO: currently only GC call eraseInternal!
  if (isGC) GCQueue.push_back(PAL::Request(req)); 
  pPAL->erase(req, tick);

  // Check erase count
  uint32_t erasedCount = block->second.getEraseCount();

  if (erasedCount < threshold) {
    // Reverse search
    auto iter = freeBlocks.end();

    while (true) {
      iter--;

      if (iter->getEraseCount() <= erasedCount) {
        // emplace: insert before pos
        iter++;

        break;
      }

      if (iter == freeBlocks.begin()) {
        break;
      }
    }

    // Insert block to free block list
    freeBlocks.emplace(iter, std::move(block->second));
    nFreeBlocks++;
  }

  // Remove block from block list
  blocks.erase(block);

  applyLatency(CPU::FTL__PAGE_MAPPING, CPU::ERASE_INTERNAL, tick);
}

float PageMapping::calculateWearLeveling() {
  uint64_t totalEraseCnt = 0;
  uint64_t sumOfSquaredEraseCnt = 0;
  uint64_t numOfBlocks = param.totalLogicalBlocks;
  uint64_t eraseCnt;

  for (auto &iter : blocks) {
    eraseCnt = iter.second.getEraseCount();
    totalEraseCnt += eraseCnt;
    sumOfSquaredEraseCnt += eraseCnt * eraseCnt;
  }

  // freeBlocks is sorted
  // Calculate from backward, stop when eraseCnt is zero
  for (auto riter = freeBlocks.rbegin(); riter != freeBlocks.rend(); riter++) {
    eraseCnt = riter->getEraseCount();

    if (eraseCnt == 0) {
      break;
    }

    totalEraseCnt += eraseCnt;
    sumOfSquaredEraseCnt += eraseCnt * eraseCnt;
  }

  if (sumOfSquaredEraseCnt == 0) {
    return -1;  // no meaning of wear-leveling
  }

  return (float)totalEraseCnt * totalEraseCnt /
         (numOfBlocks * sumOfSquaredEraseCnt);
}

void PageMapping::calculateTotalPages(uint64_t &valid, uint64_t &invalid) {
  valid = 0;
  invalid = 0;

  for (auto &iter : blocks) {
    valid += iter.second.getValidPageCount();
    invalid += iter.second.getDirtyPageCount();
  }
}

void PageMapping::getStatList(std::vector<Stats> &list, std::string prefix) {
  Stats temp;

  temp.name = prefix + "page_mapping.gc.count";
  temp.desc = "Total GC count";
  list.push_back(temp);

  temp.name = prefix + "page_mapping.gc.reclaimed_blocks";
  temp.desc = "Total reclaimed blocks in GC";
  list.push_back(temp);

  temp.name = prefix + "page_mapping.gc.superpage_copies";
  temp.desc = "Total copied valid superpages during GC";
  list.push_back(temp);

  temp.name = prefix + "page_mapping.gc.page_copies";
  temp.desc = "Total copied valid pages during GC";
  list.push_back(temp);

  // For the exact definition, see following paper:
  // Li, Yongkun, Patrick PC Lee, and John Lui.
  // "Stochastic modeling of large-scale solid-state storage systems: analysis,
  // design tradeoffs and optimization." ACM SIGMETRICS (2013)
  temp.name = prefix + "page_mapping.wear_leveling";
  temp.desc = "Wear-leveling factor";
  list.push_back(temp);
}

void PageMapping::getStatValues(std::vector<double> &values) {
  values.push_back(stat.gcCount);
  values.push_back(stat.reclaimedBlocks);
  values.push_back(stat.validSuperPageCopies);
  values.push_back(stat.validPageCopies);
  values.push_back(calculateWearLeveling());
}

void PageMapping::resetStatValues() {
  memset(&stat, 0, sizeof(stat));
}

}  // namespace FTL

}  // namespace SimpleSSD
