/**
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
 *
 * Authors:
 */
#ifndef __PAL_async_h__
#define __PAL_async_h__

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>

#include "PALa_ParaModule.h"
#include "PALa_ReqSM.h"
#include "pal/old/Latency.h"
#include "pal/old/PALStatistics.h"
#include "pal/pal.hh"
#include "util/old/SimpleSSD_types.h"
#include "error/retry.hh"

class PALStatistics;

class PALasync {
  // YUDA: belows are copied from PAL2.
  SimpleSSD::PAL::Parameter *pParam;
  Latency *lat;
  PALStatistics *stats;

  bool useMultiPlane;
  bool useAMPI;
  bool useSuspend;
  bool useSCA;

  uint64_t maxPageWriteSuspend;
  uint64_t maxBlockEraseSuspend;

  uint64_t totalDie;
  uint64_t curReqID;

  std::vector<Channel> channels;
  std::vector<std::pair<PALRequestSM *,
                        std::pair<SimpleSSD::PAL::Request *, uint64_t *>>>
      requests;

  std::function<void()> commitCallback;

 public:
  PALasync(PALStatistics *s, SimpleSSD::PAL::Parameter *p,
           SimpleSSD::ConfigReader *, Latency *l);
  ~PALasync();

  void setCommitCallBack(std::function<void()> f) { commitCallback = f; }
  void submit(Command &cmd, CPDPBP &addr, SimpleSSD::PAL::Request *pReq,
              uint64_t *pCounter, SimpleSSD::ERROR::Retry *retry);
  void commitReq(uint64_t tick, uint64_t id);  
  
  void getStatList(std::vector<SimpleSSD::Stats> &list, std::string prefix);
  void getStatValues(std::vector<double> &values);
  void resetStatValues();
};

#endif  // __PAL_async_h__