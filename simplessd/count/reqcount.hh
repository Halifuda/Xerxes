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

#pragma once

#ifndef __COUNT_REQCOUNT__
#define __COUNT_REQCOUNT__

#include "util/old/SimpleSSD_types.h"
#include "sim/config_reader.hh"

namespace SimpleSSD {

namespace Count{

class ReqCount {
 private:
   ConfigReader &conf;
 public:
  ReqCount(ConfigReader &);
  ~ReqCount();
  void checkseq(uint64_t begin, uint64_t length);

  uint64_t seqreq;
  uint64_t seqlength;

  uint64_t randreq;
  uint64_t randlength;

  uint64_t lastend;
  uint64_t lastlength;
  
  bool isfirst;

  bool use;
};


void InitReqCount(ConfigReader &);

void CheckReadSeq(uint64_t begin, uint64_t length);

void CheckWriteSeq(uint64_t begin, uint64_t length);

void deInitReqCount();

void printReqCount();

}

}  // namespace SimpleSSD


#endif
