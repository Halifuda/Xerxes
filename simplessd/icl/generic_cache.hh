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

#ifndef __ICL_GENERIC_CACHE__
#define __ICL_GENERIC_CACHE__

#include <functional>
#include <random>
#include <vector>

#include "icl/abstract_cache.hh"

namespace SimpleSSD {

namespace ICL {

class GenericCache : public AbstractCache {
 private:
  const uint32_t superPageSize;
  const uint32_t parallelIO;
  uint32_t lineCountInSuperPage;
  uint32_t mappingUnitInLine;
  uint32_t lineCountInMaxIO;
  uint32_t lineSize;
  uint32_t setSize;
  uint32_t waySize;
  uint64_t internalID;

  const uint32_t prefetchIOCount;
  const float prefetchIORatio;

  const bool useReadCaching;
  const bool useWriteCaching;
  const bool useReadPrefetch;

  bool bSuperPage;

  struct SequentialDetect {
    bool enabled;
    Request lastRequest;
    uint32_t hitCounter;
    uint32_t accessCounter;

    SequentialDetect() : enabled(false), hitCounter(0), accessCounter(0) {
      lastRequest.reqID = 1;
    }
  } readDetect;

  uint64_t prefetchTrigger;
  uint64_t lastPrefetched;

  PREFETCH_MODE prefetchMode;
  WRITE_POLICY writePolicy;
  EVICT_MODE evictMode;
  EVICT_POLICY policy;
  std::function<uint32_t(uint32_t, uint64_t &)> evictFunction;
  std::function<Line *(Line *, Line *)> compareFunction;
  std::random_device rd;
  std::mt19937 gen;
  std::uniform_int_distribution<uint32_t> dist;

  std::vector<Line *> cacheData;
  std::vector<Line *> evictData;

  uint64_t getCacheLatency();

  uint32_t calcSetIndex(uint64_t);
  void calcIOPosition(uint64_t, uint64_t, uint32_t &);

  uint32_t getEmptyWay(uint32_t, uint64_t &);
  uint32_t getValidWay(uint64_t, uint32_t, uint64_t &);
  bool caculateEvictLine(uint32_t, uint64_t &);
  void checkSequential(Request &, SequentialDetect &);

  void evictCache(Request *, cacheReqContext *, uint64_t &, uint64_t, bool = true);
  bool evictCheckHasNoEvictingInSet(Request *);

  bool readInternal(Request &, uint64_t &);
  bool writeInternal(Request &, uint64_t &, bool);

  // Stats
  struct {
    uint64_t request[2];
    uint64_t cache[2];
  } stat;

  typedef uint64_t LCA;
  typedef struct {
    ReqKey iclReqId;
    uint64_t ftlReqId;
  } CoalescedReq;
  std::unordered_map<LCA, std::vector<CoalescedReq>> FTLReadCoalesceMap;

  /// @brief Check if a FTL Read request can be coalesced to a
  /// previous one. If so, coalesced it, otherwise record it to
  /// let succeeding Reads can be coalesced.
  /// @param read FTL Read Request.
  /// @return bool. `true` if can be coalesced, the request will
  /// be coalesced; `false` if cannot be coalesced, the request 
  /// will be inserted into the coalescing recording map.
  bool coalesce_CheckOrInsert(FTL::Request &);

   /// @brief Only check, don't insert
  /// @param read FTL Read Request.
  /// @return bool. `true` if can be coalesced, the request will
  /// be coalesced; `false` if cannot be coalesced, the request 
  /// will be inserted into the coalescing recording map.
  bool coalesce_Check(FTL::Request &);

  /// @brief Once a FTL Read request is commited, this function
  /// check if there is any FTL Read being coalesced to this. 
  /// If there is, remove it from the ICL SubReq Context ftllist.
  /// Update context if needed.
  /// @param commit FTL Read Request that being commited.
  void coalesce_FTLCommit(FTL::Request &);

  uint64_t coalesce_getReqLCA(FTL::Request &);

  void updateCacheline(uint64_t, cacheUpdateContext *) override;
  void updateContext(cacheReqContext &) override;

 public:
  GenericCache(ConfigReader &, FTL::FTL *, DRAM::AbstractDRAM *);
  ~GenericCache();

  void updateSubmit(bool toftl) override;
  void Commit() override;
  bool commitToUpper(Request &) override;

  void read(Request &, uint64_t &) override;
  void write(Request &, uint64_t &) override;

  void flush(Request &, uint64_t &) override;
  void trim(Request &, uint64_t &) override;
  void format(Request &, uint64_t &) override;

  void getStatList(std::vector<Stats> &, std::string) override;
  void getStatValues(std::vector<double> &) override;
  void resetStatValues() override;
};

}  // namespace ICL

}  // namespace SimpleSSD

#endif
