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

#include "ftl/ftl.hh"

#include "ftl/page_mapping.hh"

namespace SimpleSSD {

namespace FTL {

FTL::FTL(ConfigReader &c, DRAM::AbstractDRAM *d) : conf(c), pDRAM(d) {
  PAL::Parameter *palparam;

  pPAL = new PAL::PAL(conf);
  palparam = pPAL->getInfo();

  param.totalPhysicalBlocks = palparam->superBlock;
  param.totalLogicalBlocks =
      palparam->superBlock *
      (1 - conf.readFloat(CONFIG_FTL, FTL_OVERPROVISION_RATIO));
  param.pagesInBlock = palparam->page;
  param.pageSize = palparam->superPageSize;
  param.subpageInUnit = palparam->subpageNum;
  param.ioUnitInPage = palparam->pageInSuperPage;
  param.pageCountToMaxPerf = palparam->superBlock / palparam->block;

  switch (conf.readInt(CONFIG_FTL, FTL_MAPPING_MODE)) {
    case PAGE_MAPPING:
      pFTL = new PageMapping(conf, param, pPAL, pDRAM);
      break;
  }

  if (param.totalPhysicalBlocks <=
      param.totalLogicalBlocks + param.pageCountToMaxPerf) {
    panic("FTL Over-Provision Ratio is too small");
  }

  // Print mapping Information
  debugprint(LOG_FTL, "Total physical blocks %u", param.totalPhysicalBlocks);
  debugprint(LOG_FTL, "Total logical blocks %u", param.totalLogicalBlocks);
  debugprint(LOG_FTL, "Logical page size %u", param.pageSize);

  pFTL->setCommitCallback([this]() { Commit(); });
  // Initialize pFTL
  pFTL->initialize();
}

FTL::~FTL() {
  delete pPAL;
  delete pFTL;
}

void FTL::Commit() {
  PAL::Request palreq(0);
  while (pFTL->commitToUpper(palreq)) {
    auto iter = submitQueue.begin();
    bool ftlfind = false;
    while (iter != submitQueue.end()) {
      auto ftlreq = iter->first;
      if (palreq.reqID == ftlreq.reqID && 
          palreq.reqSubID == ftlreq.reqSubID &&
          palreq.ftlSubID == ftlreq.ftlSubID) {
        ftlfind = true;
        bool palfind = false;
        for (auto i = iter->second.begin(); i != iter->second.end(); ++i) {
          if (i->blockIndex == palreq.blockIndex &&
              i->pageIndex == palreq.pageIndex &&
              i->reqType == palreq.reqType) {
            palfind = true;
            iter->second.erase(i);
            if(palreq.coalesced){
              iter->first.backInfo.clear();
              
            }
            debugprint(
                LOG_FTL,
                "PAL commit ID=%lu-%lu-%lu, addr=b%lu-p%lu, @%lu-%lu (%lu), remain %lu",
                palreq.reqID, palreq.reqSubID, palreq.ftlSubID, palreq.blockIndex,
                palreq.pageIndex, palreq.beginAt, palreq.finishAt,
                palreq.finishAt - palreq.beginAt, iter->second.size());
            break;
          }
        }
        if (palfind == false) {
          panic("Unexpected PAL req addr b%u-p%u", palreq.blockIndex,
                palreq.pageIndex);
        }
        else {
          iter->first.finishAt = MAX(iter->first.finishAt, palreq.finishAt);
        }
      }
      ++iter;
    }
    if (ftlfind == false) {
      panic("Unexpected PAL req ID %lu-%lu-%lu", 
            palreq.reqID, palreq.reqSubID, palreq.ftlSubID);
    }
  }
  updateCommit();
  commitCallback();
}

void FTL::updateCommit() {
  bool find = false;
  do {
    find = false;
    auto iter = submitQueue.begin();
    while (iter != submitQueue.end()) {
      if (iter->second.size() == 0) {
        find = true;
        break;
      }
      ++iter;
    }
    if (find) {
      Request newreq = iter->first;
      CPU::FUNCTION func = CPU::READ;
      if (newreq.reqType == WRITE) func = CPU::WRITE;
      if (newreq.reqType == TRIM) func = CPU::TRIM;
      applyLatency(CPU::FTL, func, newreq.finishAt);
      commitQueue.push_back(newreq);
      submitQueue.erase(iter);
    }
  } while (find);
}

bool FTL::commitToUpper(Request &req) {
  if (commitQueue.empty()) {
    return false;
  }
  req = commitQueue.front();
  commitQueue.pop_front();
  return true;
}

void FTL::updateSubmit() {
  auto iter = receiveQueue.begin();
  while (iter != receiveQueue.end()) {
    auto cur = iter;
    ++iter;
    _PALReqList newlist;
    submitQueue.push_back(make_pair(Request(*cur), newlist));
    receiveQueue.erase(cur);
    auto pair = submitQueue.end();
    pair--;
    auto &req = pair->first;
    auto tick = req.beginAt;
    switch (req.reqType) {
      case READ:
         debugprint(LOG_FTL, "READ  ID=%lu-%lu-%lu | LPN %" PRIu64, req.reqID,
              req.reqSubID, req.ftlSubID, req.reqInfo[0].lpn);
        pFTL->read(&pair->second, req, tick);
        break;
      case WRITE:
        debugprint(LOG_FTL, "WRITE  ID=%lu-%lu-%lu | LPN %" PRIu64, req.reqID,
              req.reqSubID, req.ftlSubID, req.reqInfo[0].lpn);
        pFTL->write(&pair->second, req, tick);
        break;
      case TRIM:
        debugprint(LOG_FTL, "WRITE  ID=%lu-%lu-%lu | LPN %" PRIu64, req.reqID,
              req.reqSubID, req.ftlSubID, req.reqInfo[0].lpn);
        pFTL->trim(&pair->second, req, tick);
        break;
      default:
        panic("FTL meets unexpected request Type %d", req.reqType);
        break;
    }
  }
  pFTL->updateSubmit();
  updateCommit();
  if (!commitQueue.empty()) commitCallback();
}

void FTL::read(Request &req, uint64_t &) {
  receiveQueue.push_back(Request(req));
}

void FTL::write(Request &req, uint64_t &) {
  receiveQueue.push_back(Request(req));
}

void FTL::trim(Request &req, uint64_t &) {
  receiveQueue.push_back(Request(req));
}

void FTL::format(LPNRange &range, uint64_t &tick) {
  debugprint(LOG_FTL, "FORMAT  | SLPN %" PRIu64, range.slpn);
  pFTL->format(range, tick);

  applyLatency(CPU::FTL, CPU::FORMAT, tick);
}

Parameter *FTL::getInfo() {
  return &param;
}

uint64_t FTL::getUsedPageCount(uint64_t lpnBegin, uint64_t lpnEnd) {
  return pFTL->getStatus(lpnBegin, lpnEnd)->mappedLogicalPages;
}

void FTL::getStatList(std::vector<Stats> &list, std::string prefix) {
  pFTL->getStatList(list, prefix + "ftl.");
  pPAL->getStatList(list, prefix);
}

void FTL::getStatValues(std::vector<double> &values) {
  pFTL->getStatValues(values);
  pPAL->getStatValues(values);
}

void FTL::resetStatValues() {
  pFTL->resetStatValues();
  pPAL->resetStatValues();
}

}  // namespace FTL

}  // namespace SimpleSSD
