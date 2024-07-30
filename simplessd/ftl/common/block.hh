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

#ifndef __FTL_COMMON_BLOCK__
#define __FTL_COMMON_BLOCK__

#include <cinttypes>
#include <vector>

#include "util/bitset.hh"
#include "util/def.hh"

namespace SimpleSSD {

namespace FTL {

class Block {
 private:
  uint32_t idx;
  uint32_t pageCount;
  uint32_t ioUnitInPage;
  uint32_t subPageInUnit;
  uint32_t pNextWriteUnitIndex;
  uint32_t pNextWritePageIndex;

  // Following variables are used when ioUnitInPage == 1
  Bitset *pValidBits;
  Bitset *pErasedBits;
  ReqInfo **pLPNs;

  // Following variables are used when ioUnitInPage > 1
  std::vector<Bitset> validBits;
  std::vector<Bitset> erasedBits;

  std::vector<std::vector<Bitset>> validCount;
  ReqInfo ***reMap;

  uint64_t lastAccessed;
  uint32_t eraseCount;

 public:
  Block(uint32_t, uint32_t, uint32_t, uint32_t);
  Block(const Block &);      // Copy constructor
  Block(Block &&) noexcept;  // Move constructor
  ~Block();

  Block &operator=(const Block &);  // Copy assignment
  Block &operator=(Block &&);       // Move assignment

  uint32_t getBlockIndex() const;
  uint64_t getLastAccessedTime();
  uint32_t getEraseCount();
  uint32_t getValidPageCount();
  uint32_t getValidPageCountRaw();
  uint32_t getDirtyPageCount();
  uint32_t getNextWritePageIndex();
  uint32_t getNextWriteUnitIndex();
  uint32_t getValidMappingUnit(uint32_t);
  uint32_t getFreePage();
  //uint32_t getNextWritePageIndex(uint32_t);
  bool getPageInfo(uint32_t, std::vector<std::vector<ReqInfo>> &, Bitset &);
  bool read(uint32_t, uint32_t, uint64_t, std::vector<ReqInfo> &);
  bool write(uint32_t, std::vector<ReqInfo> &, uint32_t, uint64_t, bool);
  void erase();
  void invalidate(uint32_t, uint32_t, uint32_t);
};

}  // namespace FTL

}  // namespace SimpleSSD

#endif
