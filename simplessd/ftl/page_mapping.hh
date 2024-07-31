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

#ifndef __FTL_PAGE_MAPPING__
#define __FTL_PAGE_MAPPING__

#include <cinttypes>
#include <unordered_map>
#include <vector>

#include "ftl/abstract_ftl.hh"
#include "ftl/common/block.hh"
#include "ftl/ftl.hh"
#include "pal/pal.hh"

namespace SimpleSSD {

namespace FTL {

typedef struct _MappingInfo{
  uint32_t blockIndex;
  uint32_t idx;
  uint32_t pageIndex;
  uint32_t subIdx;
  bool operator==(const _MappingInfo &);
  bool operator<(const _MappingInfo &);
  bool operator>(const _MappingInfo &);

  _MappingInfo(): blockIndex(0), idx(0), pageIndex(0), subIdx(0){}
  _MappingInfo(PAL::Request &r):
    blockIndex(r.blockIndex),
    pageIndex(r.pageIndex),
    subIdx(0){
      idx = 0;
      for(uint32_t i = 0; i < r.ioFlag.size(); i++){
        if(r.ioFlag.test(i)){
          idx = i;
          break;
        }
      }
    }
} MappingInfo;

class PageMapping : public AbstractFTL {
 private:
  PAL::PAL *pPAL;

  ConfigReader &conf;

  std::unordered_map<uint64_t, MappingInfo>
      table;
  std::unordered_map<uint32_t, Block> blocks;
  std::list<Block> freeBlocks;
  uint32_t nFreeBlocks;  // For some libraries which std::list::size() is O(n)
  std::vector<uint32_t> lastFreeBlock;
  uint32_t lastFreeBlockFreePage;
  uint32_t lastFreeBlockIndex;

  bool bReclaimMore;
  bool bRandomTweak;
  uint32_t bitsetSize;
  uint32_t subpageInUnit;

  struct {
    uint64_t gcCount;
    uint64_t reclaimedBlocks;
    uint64_t validSuperPageCopies;
    uint64_t validPageCopies;
  } stat;

  float freeBlockRatio();
  uint32_t convertBlockIdx(uint32_t);
  uint32_t getFreeBlock(uint32_t);
  uint32_t getLastFreeBlock(uint32_t);
  void calculateVictimWeight(std::vector<std::pair<uint32_t, float>> &,
                             const EVICT_POLICY, uint64_t);
  void selectVictimBlock(std::vector<uint32_t> &, uint64_t &);
  void doGarbageCollection(std::vector<uint32_t> &, uint64_t &);

  float calculateWearLeveling();
  void calculateTotalPages(uint64_t &, uint64_t &);
  uint64_t getTableIndex(ReqInfo &);
  
  void readInternal(std::vector<PAL::Request> *, Request &, uint64_t &);
  void writeInternal(std::vector<PAL::Request> *, Request &, uint64_t &, bool = true);
  void trimInternal(std::vector<PAL::Request> *, Request &, uint64_t &);
  void eraseInternal(PAL::Request &, uint64_t &, bool);

  typedef struct {
    uint64_t reqID;
    uint64_t reqSubID;
    uint64_t ftlReqID;
  } CoalescedReq;
  uint64_t mappingTo64(MappingInfo &);
  std::unordered_map<uint64_t, std::vector<CoalescedReq>> PALReadCoalesceMap;
  /// @brief Check if a PAL Read request can be coalesced to a
  /// previous one. If so, coalesced it, otherwise record it to
  /// let succeeding Reads can be coalesced.
  /// @param read PAL Read Request.
  /// @return bool. `true` if can be coalesced, the request will
  /// be coalesced; `false` if cannot be coalesced, the request 
  /// will be inserted into the coalescing recording map.
  bool coalesce_CheckOrInsert(PAL::Request &);

  /// @brief Once a PAL Read request is commited, this function
  /// check if there is any PAL Read being coalesced to this. 
  /// If there is, remove it from the outstandingQueue
  /// Update context if needed.
  /// @param commit PAL Read Request that being commited.
  void coalesce_PALCommit(PAL::Request &);

  void multiplane_Schedule();

 public:
  PageMapping(ConfigReader &, Parameter &, PAL::PAL *, DRAM::AbstractDRAM *);
  ~PageMapping();

  void updateSubmit() override;
  void Commit() override;

  bool initialize() override;

  void read(std::vector<PAL::Request> *, Request &, uint64_t &) override;
  void write(std::vector<PAL::Request> *, Request &, uint64_t &) override;
  void trim(std::vector<PAL::Request> *, Request &, uint64_t &) override;

  void format(LPNRange &, uint64_t &) override;

  Status *getStatus(uint64_t, uint64_t) override;

  void getStatList(std::vector<Stats> &, std::string) override;
  void getStatValues(std::vector<double> &) override;
  void resetStatValues() override;
};

}  // namespace FTL

}  // namespace SimpleSSD

#endif
