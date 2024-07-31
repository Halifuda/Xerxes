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

#ifndef __DRAM_SIMPLE__
#define __DRAM_SIMPLE__

#include <list>

#include "dram/abstract_dram.hh"

namespace SimpleSSD {

namespace DRAM {

class SimpleDRAM : public AbstractDRAM {
 private:
  struct Stat {
    uint64_t count;
    uint64_t size;
    uint64_t page;
    uint64_t tick;

    Stat();
  };

  uint64_t pageFetchLatency;
  double interfaceBandwidth;

  uint64_t lastFTLAccess;
  uint64_t startPoint;
  bool ignoreScheduling;

  Event autoRefresh;
  Event flushEvent;

  Stat readStat;
  Stat writeStat;

  std::map<uint64_t, uint64_t> FreeSlots;

  uint64_t updateDelay(uint64_t, uint64_t &);
  void updateStats(uint64_t);
  void findFreeTime(uint64_t, uint64_t, uint64_t &);
  void insertFreeSlot(uint64_t, uint64_t, uint64_t);
  void FlushAFreeSlot(uint64_t);

 public:
  SimpleDRAM(ConfigReader &p);
  ~SimpleDRAM();

  void read(void *, uint64_t, uint64_t &) override;
  void write(void *, uint64_t, uint64_t &) override;

  void setScheduling(bool) override;
  bool isScheduling() override;

  void getStatList(std::vector<Stats> &, std::string) override;
  void getStatValues(std::vector<double> &) override;
  void resetStatValues() override;
};

}  // namespace DRAM

}  // namespace SimpleSSD

#endif
