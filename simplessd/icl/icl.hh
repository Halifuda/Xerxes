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

#ifndef __ICL_ICL__
#define __ICL_ICL__

#include "dram/abstract_dram.hh"
#include "ftl/ftl.hh"
#include "icl/abstract_cache.hh"
#include "util/simplessd.hh"

namespace SimpleSSD {

namespace ICL {
class ICL : public StatObject {
 private:
  FTL::FTL *pFTL;
  DRAM::AbstractDRAM *pDRAM;

  ConfigReader &conf;
  AbstractCache *pCache;

  uint64_t totalLogicalPages;
  uint32_t logicalPageSize;

  // Yuda: submit queue: ICL will split a HIL request into several
  // page-granular sub-requests, use a count to record how
  // many related sub-requests are commited.
  std::list<std::pair<Request, int64_t>> submitQueue;

  // Yuda: commit queue
  std::list<Request> commitQueue;

  std::function<void()> commitCallback;

 public:
  ICL(ConfigReader &);
  ~ICL();

  bool submitQueueIsFull() {
    // AYDTODO: queuedepth?
    // return submitQueue.size() >= submitQueueDepth;
    return false;
  }
  void setCommitCallback(std::function<void()> f) { commitCallback = f; }

  void updateSubmit();
  void updateCommit();
  bool commitToUpper(Request &);
  void Commit();

  void read(Request &, uint64_t &);
  void write(Request &, uint64_t &);

  void flush(Request &, uint64_t &);
  void trim(Request &, uint64_t &);
  void format(Request &, uint64_t &);

  void getLPNInfo(uint64_t &, uint32_t &);
  uint64_t getUsedPageCount(uint64_t, uint64_t);

  void getStatList(std::vector<Stats> &, std::string) override;
  void getStatValues(std::vector<double> &) override;
  void resetStatValues() override;
};

}  // namespace ICL

}  // namespace SimpleSSD

#endif
