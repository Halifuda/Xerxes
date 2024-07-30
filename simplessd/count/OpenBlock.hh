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

#ifndef __COUNT_OPENBLOCK__
#define __COUNT_OPENBLOCK__

#include "util/old/SimpleSSD_types.h"
#include "sim/config_reader.hh"
#include "lib/mcpat/mcpat.h"
#include <list>

namespace SimpleSSD {

namespace Count{

struct OpenInterval {

  uint64_t begintick1;
  uint64_t endtick1;

  uint64_t begintick2;
  uint64_t endtick2;

  OpenInterval(uint64_t begin1, uint64_t end1, uint64_t begin2, uint64_t end2);

};

struct OpenBlock{
    uint64_t lastbegin;
    uint64_t lastend;
    std::list<OpenInterval> OBlist;

    void checkinterval(uint64_t begintick, uint64_t endtick, uint64_t threshrod);
    uint64_t size();
};

class Openb{
  private:
  OpenBlock **table;
  ConfigReader &conf;
  uint64_t  RecordThreshrod;
  bool ifuse;

  public:
  uint32_t totalblock;
  Openb(ConfigReader &);
  ~Openb();
  void settable(uint32_t nblock);
  void printout();
  void insert(uint32_t block, uint64_t begintick, uint64_t endtick);
};


void InitOpenBlock(ConfigReader &);

void Settable(uint32_t nblock);

void InsertOpenBlock(uint32_t block, uint64_t begintick, uint64_t endtick);

void printOpenBlock();

void deInitOpenBlock();


}

}  // namespace SimpleSSD


#endif
