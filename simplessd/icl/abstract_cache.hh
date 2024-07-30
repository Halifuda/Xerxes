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

#ifndef __ICL_CACHE__
#define __ICL_CACHE__

#include <list>
#include <map>
#include <utility>
#include <vector>

#include "dram/abstract_dram.hh"
#include "ftl/ftl.hh"
#include "util/simplessd.hh"

namespace SimpleSSD {

namespace ICL {

typedef struct _Line {
  uint64_t tag;
  uint64_t lastAccessed;
  uint64_t insertedAt;
  uint32_t subIdx;
  bool dirty;
  bool valid;
  bool waiting;
  bool evicting;

  _Line();
  _Line(uint64_t, bool);
} Line;

struct ReqKeyLess {
  bool operator()(const ReqKey &a, const ReqKey &b) const {
    if (a.id == b.id) {
      return a.subid < b.subid;
    }
    return a.id < b.id;
  }
};

class AbstractCache : public StatObject {
 protected:
  ConfigReader &conf;
  FTL::FTL *pFTL;
  DRAM::AbstractDRAM *pDRAM;

  typedef std::vector<std::pair<uint64_t, uint64_t>> _cacheLineList;

  struct cacheReqContext;

  typedef std::function<bool(uint64_t, cacheReqContext *)> ReqFunc;

  /// @brief YUDA: Context to update a cacheline.
  struct cacheUpdateContext {
    bool valid, dirty, waiting;
    uint32_t set, way, subIdx;
    uint64_t lca, tag;
    Line *pLine;
  };

  /// @brief YUDA: A recorder for context of an ICL request.
  /// Steps may involve with cacheline update, DRAM access
  /// and FTL request. Only FTL requests need to be waited.
  struct cacheReqContext {
    bool hit;
    bool iswaiting;
    bool is_sequential;
    bool to_ftl;
    bool fromRead;
    /// @brief FTL request sub id, used for hashmap
    uint64_t ftlReqId;
    /// @brief related cache index
    uint32_t setIdx, wayIdx;
    uint64_t arrived;
    Request req;
    /// @brief A list of functions handling request step by step.
    /// @param beginAt The call tick of a step.
    /// @param c A pointer to [cacheReqContext].
    /// @return [bool]. <false> when this function needs not to send
    /// FTL requests, the next function can immediately fire.
    /// <true> when this function needs to send FTL requests,
    /// the next function should wait for FTL commit.
    std::list<ReqFunc> steps;
    /// @brief A list recording the outstanding FTL requests dealing
    /// with current step. The key is <ftlSubID>, which is given and 
    /// managed by this context. The [cacheUpdateContext] records the
    /// related cacheline this FTL request is going to update.
    std::unordered_map<uint64_t, std::pair<FTL::Request, cacheUpdateContext *>>
        ftllist;
  };

  /// @brief YUDA: A map recording ICL Subrequests. 
  ///
  /// The [Key] is ReqKey, a struct storing request id and sub id. The 
  /// id is given by HIL, tagging IO blocks; the sub id is given by ICL 
  /// (upper), tagging superpages in each IO block. 
  /// 
  /// The [Hash] is a simple function generating uint64_t value from id
  /// and sub id. You can modify it in util/def.hh or write a new one.
  ///
  /// The [Value] is cacheReqContext, please see above.
  std::map<ReqKey, cacheReqContext, ReqKeyLess> submitQueue;
  std::list<Request> commitQueue;

  /// @brief YUDA: Context used for cacheline eviction.
  /// Evict-FTL-requests are recorded individually.
  struct evictContext {
    FTL::Request ftlReq;
    bool flush;
    Line **evict;
    evictContext() : ftlReq(0), flush(false), evict(nullptr) {}
    evictContext(FTL::Request &ftlReq_, bool flush_, Line **evict_)
        : ftlReq(ftlReq_), flush(flush_), evict(evict_) {}
  };

  std::list<evictContext> evictQueue;

  std::function<void()> commitCallback;

  virtual void updateCacheline(uint64_t, cacheUpdateContext *) = 0;
  virtual void updateContext(cacheReqContext &) = 0;

 public:
  AbstractCache(ConfigReader &, FTL::FTL *, DRAM::AbstractDRAM *);
  virtual ~AbstractCache();

  void setCommitCallback(std::function<void()> f) { commitCallback = f; }

  virtual void updateSubmit(bool) = 0;
  virtual void Commit() = 0;
  virtual bool commitToUpper(Request &) = 0;

  virtual void read(Request &, uint64_t &) = 0;
  virtual void write(Request &, uint64_t &) = 0;

  virtual void flush(Request &, uint64_t &) = 0;
  virtual void trim(Request &, uint64_t &) = 0;
  virtual void format(Request &, uint64_t &) = 0;
};

}  // namespace ICL

}  // namespace SimpleSSD

#endif
