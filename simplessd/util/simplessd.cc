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

#include "util/simplessd.hh"
#include "count/Counts.hh"
#include "count/reqcount.hh"
#include "count/OpenBlock.hh"
#include "sim/log.hh"
#include "error/block.hh"

using namespace SimpleSSD;

ConfigReader initSimpleSSDEngine(Simulator *sim, std::ostream *info,
                                 std::ostream *err, std::ostream *p, std::ostream *c, std::ostream *r, std::string config) {
  ConfigReader conf;

  setSimulator(sim);
  initLogSystem(info, err, p, c, r);

  if (!conf.init(config)) {
    panic("Failed to open configuration file %s", config.c_str());
  }

  initCPU(conf);
  Count::InitOpenBlock(conf);
  Count::InitOpCount(conf);
  Count::InitReqCount(conf);
  ERROR::InitBlockState(conf);
  return conf;
}

void releaseSimpleSSDEngine() {
  printCPULastStat();

  Count::printOpCount();
  Count::printReqCount();
  Count::printOpenBlock();
  Count::deInitReqCount();
  Count::deInitOpCount();
  Count::deInitOpenBlock();
  ERROR::deInitBlockState();
  
  deInitCPU();
}
