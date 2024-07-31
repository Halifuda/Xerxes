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

#include "ftl/common/block.hh"

#include <algorithm>
#include <cstring>

namespace SimpleSSD {

namespace FTL {

Block::Block(uint32_t blockIdx, uint32_t count, uint32_t ioUnit, uint32_t subPage)
    : idx(blockIdx),
      pageCount(count),
      ioUnitInPage(ioUnit),
      subPageInUnit(subPage),
      pValidBits(nullptr),
      pErasedBits(nullptr),
      pLPNs(nullptr),
      reMap(nullptr),
      lastAccessed(0),
      eraseCount(0) {
  if (ioUnitInPage == 1) {
    pValidBits = new Bitset(pageCount);
    pErasedBits = new Bitset(pageCount);

    pLPNs = (ReqInfo **)calloc(pageCount, sizeof(ReqInfo *));
    for(uint32_t i = 0; i < pageCount; i++){
      pLPNs[i] = (ReqInfo *)calloc(subPageInUnit, sizeof(ReqInfo));
    }
  }
  else if (ioUnitInPage > 1) {
    Bitset copy(ioUnitInPage);
    Bitset copy2(subPageInUnit);
    validBits = std::vector<Bitset>(pageCount, copy);
    erasedBits = std::vector<Bitset>(pageCount, copy);

    reMap = (ReqInfo ***)calloc(pageCount, sizeof(ReqInfo **));

    for (uint32_t i = 0; i < pageCount; i++) {
      reMap[i] = (ReqInfo **)calloc(ioUnitInPage, sizeof(ReqInfo *));
      for(uint32_t j = 0; j < ioUnitInPage; j++){
        reMap[i][j] = (ReqInfo *)calloc(subPageInUnit, sizeof(ReqInfo));
      }
    }
  }
  else {
    panic("Invalid I/O unit in page");
  }
  validCount = std::vector<std::vector<Bitset>>(pageCount, std::vector<Bitset>(ioUnitInPage, Bitset(subPageInUnit)));
  // C-style allocation
  pNextWritePageIndex = 0;
  pNextWriteUnitIndex = 0;

  erase();
  eraseCount = 0;
}

Block::Block(const Block &old)
    : Block(old.idx, old.pageCount, old.ioUnitInPage, old.subPageInUnit) {
  if (ioUnitInPage == 1) {
    *pValidBits = *old.pValidBits;
    *pErasedBits = *old.pErasedBits;

    for(uint32_t i = 0; i < pageCount; i++){
      memcpy(pLPNs[i], old.pLPNs[i], subPageInUnit * sizeof(ReqInfo));
    }
  }
  else {
    validBits = old.validBits;
    erasedBits = old.erasedBits;
    validCount = old.validCount;

    for (uint32_t i = 0; i < pageCount; i++) {
      for(uint32_t j = 0; j < ioUnitInPage; j++){
        memcpy(reMap[i][j], old.reMap[i][j], subPageInUnit * sizeof(ReqInfo));
      }
    }
  }

  pNextWritePageIndex = old.pNextWritePageIndex;
  pNextWriteUnitIndex = old.pNextWriteUnitIndex;
  eraseCount = old.eraseCount;
}

Block::Block(Block &&old) noexcept
    : idx(std::move(old.idx)),
      pageCount(std::move(old.pageCount)),
      ioUnitInPage(std::move(old.ioUnitInPage)),
      subPageInUnit(std::move(old.subPageInUnit)),
      pNextWriteUnitIndex(std::move(old.pNextWriteUnitIndex)),
      pNextWritePageIndex(std::move(old.pNextWritePageIndex)),
      pValidBits(std::move(old.pValidBits)),
      pErasedBits(std::move(old.pErasedBits)),
      pLPNs(std::move(old.pLPNs)),
      validBits(std::move(old.validBits)),
      erasedBits(std::move(old.erasedBits)),
      validCount(std::move(old.validCount)),
      reMap(std::move(old.reMap)),
      lastAccessed(std::move(old.lastAccessed)),
      eraseCount(std::move(old.eraseCount)) {
  // TODO Use std::exchange to set old value to null (C++14)
  old.idx = 0;
  old.pageCount = 0;
  old.ioUnitInPage = 0;
  old.pNextWritePageIndex = 0;
  old.pNextWriteUnitIndex = 0;
  old.pValidBits = nullptr;
  old.pErasedBits = nullptr;
  old.pLPNs = nullptr;
  old.reMap = nullptr;
  old.lastAccessed = 0;
  old.eraseCount = 0;
}

Block::~Block() {
  if (pLPNs) {
    for(uint32_t i = 0; i < pageCount; i++){
      free(pLPNs[i]);
    }
    free(pLPNs);
  }

  delete pValidBits;
  delete pErasedBits;


  if (reMap) {
    for (uint32_t i = 0; i < pageCount; i++) {
      for(uint32_t j = 0;j < ioUnitInPage; j++){
        free(reMap[i][j]);
      }
      free(reMap[i]);
    }

    free(reMap);
  }

  pLPNs = nullptr;
  pValidBits = nullptr;
  pErasedBits = nullptr;
  reMap = nullptr;
}

Block &Block::operator=(const Block &rhs) {
  if (this != &rhs) {
    this->~Block();
    *this = Block(rhs);  // Call copy constructor
  }

  return *this;
}

Block &Block::operator=(Block &&rhs) {
  if (this != &rhs) {
    this->~Block();

    idx = std::move(rhs.idx);
    pageCount = std::move(rhs.pageCount);
    ioUnitInPage = std::move(rhs.ioUnitInPage);
    subPageInUnit = std::move(rhs.subPageInUnit);
    pNextWritePageIndex = std::move(rhs.pNextWritePageIndex);
    pNextWriteUnitIndex = std::move(rhs.pNextWriteUnitIndex);
    pValidBits = std::move(rhs.pValidBits);
    pErasedBits = std::move(rhs.pErasedBits);
    pLPNs = std::move(rhs.pLPNs);
    validBits = std::move(rhs.validBits);
    erasedBits = std::move(rhs.erasedBits);
    validCount = std::move(rhs.validCount);
    reMap = std::move(rhs.reMap);
    lastAccessed = std::move(rhs.lastAccessed);
    eraseCount = std::move(rhs.eraseCount);

    rhs.pValidBits = nullptr;
    rhs.pErasedBits = nullptr;
    rhs.pLPNs = nullptr;
    rhs.reMap = nullptr;
    rhs.lastAccessed = 0;
    rhs.eraseCount = 0;
  }

  return *this;
}

uint32_t Block::getBlockIndex() const {
  return idx;
}

uint64_t Block::getLastAccessedTime() {
  return lastAccessed;
}

uint32_t Block::getEraseCount() {
  return eraseCount;
}

uint32_t Block::getValidPageCount() {
  uint32_t ret = 0;

  if (ioUnitInPage == 1) {
    ret = pValidBits->count();
  }
  else {
    for (auto &iter : validBits) {
      if (iter.any()) {
        ret++;
      }
    }
  }

  return ret;
}

uint32_t Block::getValidPageCountRaw() {
  uint32_t ret = 0;

  if (ioUnitInPage == 1) {
    // Same as getValidPageCount()
    ret = pValidBits->count();
  }
  else {
    for (auto &iter : validBits) {
      ret += iter.count();
    }
  }

  return ret;
}

uint32_t Block::getDirtyPageCount() {
  uint32_t ret = 0;

  if (ioUnitInPage == 1) {
    ret = (~(*pValidBits | *pErasedBits)).count();
  }
  else {
    for (uint32_t i = 0; i < pageCount; i++) {
      // Dirty: Valid(false), Erased(false)
      if ((~(validBits.at(i) | erasedBits.at(i))).any()) {
        ret++;
      }
    }
  }

  return ret;
}

uint32_t Block::getNextWritePageIndex() {
  return pNextWritePageIndex;
}

uint32_t Block::getNextWriteUnitIndex() {
  return pNextWriteUnitIndex;
}

uint32_t Block::getValidMappingUnit(uint32_t pageIndex){
  uint32_t count = 0;
  if(ioUnitInPage == 1){
    return validCount[pageIndex][0].count();
  }
  for(uint32_t idx = 0; idx < ioUnitInPage; idx++){
    count += validCount[pageIndex][idx].count();
  }
  return count;
}

uint32_t Block::getFreePage() {
  return (pageCount - pNextWritePageIndex) * ioUnitInPage
    - pNextWriteUnitIndex; 
}

bool Block::getPageInfo(uint32_t pageIndex, std::vector<std::vector<ReqInfo>> &lpn,
                        Bitset &map) {
  if (ioUnitInPage == 1 && map.size() == 1) {
    if(pValidBits->test(pageIndex)){
      map.set();
    }
    else{
      map.reset();
    }

    lpn.push_back(
      std::vector<ReqInfo>(pLPNs[pageIndex], pLPNs[pageIndex] + subPageInUnit));
  }
  else if (map.size() == ioUnitInPage) {
    map = validBits.at(pageIndex);
    for(uint32_t i = 0; i < ioUnitInPage; i++){
      lpn.push_back(std::vector<ReqInfo>(reMap[pageIndex][i],
                                  reMap[pageIndex][i] + subPageInUnit));
      /*if(idx == 1387 && pageIndex == 204){
        debugprint(LOG_FTL, "TESTLPN %u", lpn[i][0].lpn);
      }*/
    }
    /*if(idx == 1387 && pageIndex == 204){
      for(uint32_t i = 0; i < ioUnitInPage; i++){
        debugprint(LOG_FTL, "BLOCKLPN %u", reMap[pageIndex][i][0].lpn);
        debugprint(LOG_FTL, "BLOCKLPN %u", lpn[i][0].lpn);
      }
    }*/
  }
  else {
    panic("I/O map size mismatch");
  }

  return map.any();
}

bool Block::read(uint32_t pageIndex, uint32_t idx, uint64_t tick,
    std::vector<ReqInfo> &bakcInfo) {
  bool read = false;

  if (ioUnitInPage == 1 && idx == 0) {
    read = pValidBits->test(pageIndex);
    for(uint32_t i = 0; i < subPageInUnit; i++){
      if(validCount[pageIndex][idx].test(i)){
        bakcInfo.push_back(pLPNs[pageIndex][i]);
      }
    }
  }
  else if (idx < ioUnitInPage) {
    read = validBits.at(pageIndex).test(idx);
    for(uint32_t i = 0; i < subPageInUnit; i++){
      if(validCount[pageIndex][idx].test(i)){
        bakcInfo.push_back(reMap[pageIndex][idx][i]);
      }
    }
  }
  else {
    panic("I/O map size mismatch");
  }

  if (read) {
    lastAccessed = tick;
  }

  return read;
}

bool Block::write(uint32_t pageIndex, std::vector<ReqInfo> &mapList, uint32_t unitIdx,
                  uint64_t tick, bool bRandomTweak) {
  bool write = false;
  if (ioUnitInPage == 1 && unitIdx == 0) {
    write = pErasedBits->test(pageIndex);
  }
  else if (unitIdx < ioUnitInPage) {
    write = erasedBits.at(pageIndex).test(unitIdx);
  }
  else {
    debugprint(LOG_FTL, "%u", unitIdx);
    panic("I/O map size mismatch");
  }

  if (write) {
    if (unitIdx < pNextWriteUnitIndex ||
       (unitIdx == pNextWriteUnitIndex && pageIndex < pNextWritePageIndex)) {
      panic("Write to block should sequential");
    }

    lastAccessed = tick;
    if (ioUnitInPage == 1) {
      pErasedBits->reset(pageIndex);
      pValidBits->set(pageIndex);
      for(uint32_t i = 0; i < subPageInUnit; i++){
        pLPNs[pageIndex][i] = mapList[i];
        if(mapList[i].subIdx < subPageInUnit){
          validCount[pageIndex][unitIdx].set(i);
        }
      }

    }
    else {
      erasedBits.at(pageIndex).reset(unitIdx);
      validBits.at(pageIndex).set(unitIdx);
      for(uint32_t i = 0; i < subPageInUnit; i++){
        reMap[pageIndex][unitIdx][i] = mapList[i];
        if(mapList[i].subIdx != subPageInUnit){
          validCount[pageIndex][unitIdx].set(i);
        }
      }
    }
    if(bRandomTweak){
      pNextWritePageIndex += (unitIdx + 1) / ioUnitInPage;
      pNextWriteUnitIndex = (unitIdx + 1) % ioUnitInPage;
    }
    else{
      pNextWritePageIndex += 1;
    }
  }
  else {
    panic("Write to non erased page");
  }

  return write;
}

void Block::erase() {
  if (ioUnitInPage == 1) {
    pValidBits->reset();
    pErasedBits->set();
  }
  else {
    for (auto &iter : validBits) {
      iter.reset();
    }
    for (auto &iter : erasedBits) {
      iter.set();
    }
  }

  pNextWritePageIndex = 0;
  pNextWriteUnitIndex = 0;

  eraseCount++;
}

void Block::invalidate(uint32_t pageIndex, uint32_t idx, uint32_t subIdx) {
  if (ioUnitInPage == 1) {
    pValidBits->reset(pageIndex);
    validCount[pageIndex][idx].reset(subIdx);
    if(validCount[pageIndex][0].none()){
      pValidBits->reset(pageIndex);
    }
    pLPNs[pageIndex][subIdx].subIdx = subPageInUnit;
  }
  else {
    validCount[pageIndex][idx].reset(subIdx);
    reMap[pageIndex][idx][subIdx].subIdx = subPageInUnit;
    if(validCount[pageIndex][idx].none()){
      validBits.at(pageIndex).reset(idx);
    }
  }
}

}  // namespace FTL

}  // namespace SimpleSSD
