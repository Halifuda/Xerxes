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

#ifndef __FTL_ABSTRACT_FTL__
#define __FTL_ABSTRACT_FTL__

#include <cinttypes>

#include "ftl/ftl.hh"

namespace SimpleSSD {

namespace FTL {

typedef struct _Status {
  uint64_t totalLogicalPages;
  uint64_t mappedLogicalPages;
  uint64_t freePhysicalBlocks;
} Status;

class AbstractFTL : public StatObject {
 protected:
  Parameter &param;
  PAL::PAL *pPAL;
  DRAM::AbstractDRAM *pDRAM;
  Status status;

  /// AYDNOTICE: temp queue to store un-sent reqs.
  std::list<PAL::Request> receiveQueue;
  std::list<PAL::Request> outstandingQueue;
  std::list<PAL::Request> GCQueue;
  std::list<PAL::Request> finishedQueue;
  /// This is used to commit to ftl. Not icl.
  std::function<void()> commitCallback;

 public:
  AbstractFTL(Parameter &p, PAL::PAL *l, DRAM::AbstractDRAM *d)
      : param(p), pPAL(l), pDRAM(d) {}
  virtual ~AbstractFTL() {}

  void setCommitCallback(std::function<void()> f) { commitCallback = f; }

  virtual void updateSubmit() = 0;
  /// @brief This is used to collect PAL requests and commit to FTL.
  /// The commitCallback() must be called in this method, or some other
  /// methods that will be called by this.
  virtual void Commit() = 0;
  bool commitToUpper(PAL::Request &req) {
    if (finishedQueue.empty()) {
      return false;
    }
    auto res = finishedQueue.front();
    finishedQueue.pop_front();
    req = res;
    return true;
  }

  virtual bool initialize() = 0;

  virtual void read(std::vector<PAL::Request> *, Request &, uint64_t &) = 0;
  virtual void write(std::vector<PAL::Request> *, Request &, uint64_t &) = 0;
  virtual void trim(std::vector<PAL::Request> *, Request &, uint64_t &) = 0;

  virtual void format(LPNRange &, uint64_t &) = 0;

  virtual Status *getStatus(uint64_t, uint64_t) = 0;
};

}  // namespace FTL

}  // namespace SimpleSSD

#endif
