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

#ifndef __COUNT_COUNTS__
#define __COUNT_COUNTS__

#include "util/old/SimpleSSD_types.h"
#include "sim/config_reader.hh"

namespace SimpleSSD {

namespace Count{

class OpCount {
 private:
  uint32_t** channelcount;
  uint32_t** diecount;
  uint32_t** blockcount;
  uint32_t** pagecount;
  ConfigReader &conf;
  bool use;
  bool recordC;
  bool recordD;
  bool recordB;
  bool recordP;

 public:
  OpCount(ConfigReader &);
  ~OpCount();
  void SetOpCount(uint32_t channel, uint32_t package, uint32_t die, uint32_t plane, uint32_t block, 
  uint32_t page);
  uint32_t channel;
  uint32_t package;
  uint32_t die;
  uint32_t plane;
  uint32_t block;
  uint32_t page;

  void AddCount(CPDPBP* pCPD, PAL_OPERATION op);
  uint32_t CPDPBPToBlock(CPDPBP* pCPD);
  void printout();

};

void InitOpCount(ConfigReader &);

void SetOpCount(uint32_t channel, uint32_t package, uint32_t die, uint32_t plane, uint32_t block, 
  uint32_t page);

void Addcount(CPDPBP* pCPDPBP, PAL_OPERATION op);

void deInitOpCount();

void printOpCount();

uint32_t CPDPBP2Block(CPDPBP* pCPD);

uint32_t BlockToChannel(uint32_t nblock);

uint32_t BlockToDie(uint32_t nblock);

uint32_t BlockToPlane(uint32_t nblock);

uint32_t BlockToblock(uint32_t nblock);


}

}  // namespace SimpleSSD
#endif
