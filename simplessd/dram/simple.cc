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

#include "dram/simple.hh"

#include "util/algorithm.hh"

namespace SimpleSSD {

namespace DRAM {

#define REFRESH_PERIOD 64000000000
#define FLUSH_PERIOD 100000000000ull    // 0.1sec
#define FLUSH_RANGE 10000000000ull    // 0.01sec

SimpleDRAM::Stat::Stat() : count(0), size(0), page(0), tick(0) {}

SimpleDRAM::SimpleDRAM(ConfigReader &p)
    : AbstractDRAM(p), lastFTLAccess(0), startPoint(0), ignoreScheduling(false) {
  pageFetchLatency = pTiming->tRP + pTiming->tRAS;
  interfaceBandwidth = 2.0 * pStructure->busWidth * pStructure->chip *
                       pStructure->channel / 8.0 / pTiming->tCK;

  autoRefresh = allocate([this](uint64_t now) {
    dramPower->doCommand(Data::MemCommand::REF, 0, now / pTiming->tCK);

    uint64_t tick = now;
    updateDelay(pTiming->tRFC, tick);

    schedule(autoRefresh, now + REFRESH_PERIOD);
  });

  flushEvent = allocate([this](uint64_t now) {
    FlushAFreeSlot(now - FLUSH_RANGE);
    schedule(flushEvent, now + FLUSH_PERIOD);
  });
  schedule(autoRefresh, getTick() + REFRESH_PERIOD);
  schedule(flushEvent, getTick() + FLUSH_PERIOD);
}

SimpleDRAM::~SimpleDRAM() {
  // DO NOTHING
}

void SimpleDRAM::findFreeTime(uint64_t tickLen,
    uint64_t tickFrom, uint64_t &startTick){
  auto e = FreeSlots.upper_bound(tickFrom);
  if(e != FreeSlots.begin()){
    e--;
    if(e->second >= tickFrom + tickLen - 1){
      startTick = e->first;
      return;
    }
    e++;
  }
  while(e != FreeSlots.end()){
    if(e->second >= e->first + tickLen - 1){
      startTick = e->first;
      return;
    }
    e++;
  }
  if(tickFrom > startPoint){
    startTick = tickFrom;
  }
  else{
    startTick = startPoint;
  }

}

void SimpleDRAM::insertFreeSlot(uint64_t tickLen, 
    uint64_t tickFrom, uint64_t startTick){
  if(startTick == startPoint){
    startPoint = startTick + tickLen;
  }
  else if(startTick > startPoint){
    if(startTick != tickFrom){
      panic("Dram visit error");
    }
    auto end = FreeSlots.end();
    if(end != FreeSlots.begin()){
      end--;
      if(end->second + 1 == startPoint){
        FreeSlots[end->first] = startTick - 1;
      }
      else{
        FreeSlots.emplace(startPoint, startTick - 1);
      }
    }

    else{
      FreeSlots.emplace(startPoint, startTick - 1);
    }
    startPoint = startTick + tickLen;
  }
  else{
    auto timeSlot = FreeSlots.find(startTick);
    uint64_t endTick = timeSlot->second;
    FreeSlots.erase(startTick);
    if(startTick < tickFrom){
      FreeSlots.emplace(startTick, tickFrom - 1);
      if(endTick > tickFrom + tickLen - 1){
        FreeSlots.emplace(tickFrom + tickLen, endTick);
      }
    }
    else if(endTick > startTick + tickLen -1){
      FreeSlots.emplace(startTick + tickLen, endTick);
    }
  }

}

uint64_t SimpleDRAM::updateDelay(uint64_t latency, uint64_t &tick) {
  uint64_t beginAt = tick;

  if (tick > 0) {
    if (ignoreScheduling) {
      tick += latency;
      debugprint(LOG_COMMON, "DRAM accesss latency %" PRIu64 "", latency);
    }
    else {
      uint64_t startTick;

      findFreeTime(latency, tick, startTick);
      //debugprint(LOG_COMMON, "Visit Dram stat at:%" PRIu64 "", startTick);
      insertFreeSlot(latency, tick, startTick);
      if(startTick > tick){
        //debugprint(LOG_COMMON, "DRAM accesss latency %" PRIu64 " - %" PRIu64 "", beginAt, latency + startTick);
        beginAt = startTick;
      }
      else{
        //debugprint(LOG_COMMON, "DRAM accesss latency %" PRIu64 " - %" PRIu64 "", beginAt, latency + tick);
        beginAt = tick;
      }
      tick = beginAt + latency;
    }
  }

  return beginAt;
}

void SimpleDRAM::FlushAFreeSlot(uint64_t currentTick) {
  auto f = FreeSlots.begin();
  auto g = FreeSlots.end();
  for (; f != g; ) {
    if (f->second < currentTick)
      FreeSlots.erase(f++);
    else
      break;
  }
  //debugprint(LOG_COMMON, "DRAM flush freeslots");
}

void SimpleDRAM::updateStats(uint64_t cycle) {
  dramPower->calcWindowEnergy(cycle);

  auto &energy = dramPower->getEnergy();
  auto &power = dramPower->getPower();

  totalEnergy += energy.window_energy;
  totalPower = power.average_power;
}

void SimpleDRAM::setScheduling(bool enable) {
  ignoreScheduling = !enable;
}

bool SimpleDRAM::isScheduling() {
  return !ignoreScheduling;
}

void SimpleDRAM::read(void *, uint64_t size, uint64_t &tick) {
  uint64_t pageCount = (size > 0) ? (size - 1) / pStructure->pageSize + 1 : 0;
  uint64_t latency =
      (uint64_t)(pageCount * (pageFetchLatency +
                              pStructure->pageSize / interfaceBandwidth));

  uint64_t beginAt = updateDelay(latency, tick);

  // DRAMPower uses cycle unit
  beginAt /= pTiming->tCK;

  dramPower->doCommand(Data::MemCommand::ACT, 0, beginAt);

  for (uint64_t i = 0; i < pageCount; i++) {
    dramPower->doCommand(Data::MemCommand::RD, 0,
                         beginAt + spec.memTimingSpec.RCD);

    beginAt += spec.memTimingSpec.RCD;
  }

  beginAt -= spec.memTimingSpec.RCD;

  dramPower->doCommand(Data::MemCommand::PRE, 0,
                       beginAt + spec.memTimingSpec.RAS);

  // Stat Update
  updateStats(beginAt + spec.memTimingSpec.RAS + spec.memTimingSpec.RP);
  readStat.count++;
  readStat.size += size;
  readStat.page += pageCount;
  readStat.tick += latency;
}

void SimpleDRAM::write(void *, uint64_t size, uint64_t &tick) {
  uint64_t pageCount = (size > 0) ? (size - 1) / pStructure->pageSize + 1 : 0;
  uint64_t latency =
      (uint64_t)(pageCount * (pageFetchLatency +
                              pStructure->pageSize / interfaceBandwidth));

  uint64_t beginAt = updateDelay(latency, tick);

  // DRAMPower uses cycle unit
  beginAt /= pTiming->tCK;

  dramPower->doCommand(Data::MemCommand::ACT, 0, beginAt);

  for (uint64_t i = 0; i < pageCount; i++) {
    dramPower->doCommand(Data::MemCommand::WR, 0,
                         beginAt + spec.memTimingSpec.RCD);

    beginAt += spec.memTimingSpec.RCD;
  }

  beginAt -= spec.memTimingSpec.RCD;

  dramPower->doCommand(Data::MemCommand::PRE, 0,
                       beginAt + spec.memTimingSpec.RAS);

  // Stat Update
  updateStats(beginAt + spec.memTimingSpec.RAS + spec.memTimingSpec.RP);
  writeStat.count++;
  writeStat.size += size;
  writeStat.page += pageCount;
  writeStat.tick += latency;
}

void SimpleDRAM::getStatList(std::vector<Stats> &list, std::string prefix) {
  Stats temp;

  AbstractDRAM::getStatList(list, prefix);

  temp.name = prefix + "read.request_count";
  temp.desc = "Read request count";
  list.push_back(temp);

  temp.name = prefix + "read.bytes";
  temp.desc = "Read data size in byte";
  list.push_back(temp);

  temp.name = prefix + "read.pages";
  temp.desc = "Read request in page";
  list.push_back(temp);

  temp.name = prefix + "read.busy";
  temp.desc = "Total busy time for read request";
  list.push_back(temp);

  temp.name = prefix + "write.request_count";
  temp.desc = "Write request count";
  list.push_back(temp);

  temp.name = prefix + "write.bytes";
  temp.desc = "Write data size in byte";
  list.push_back(temp);

  temp.name = prefix + "write.pages";
  temp.desc = "Write request in page";
  list.push_back(temp);

  temp.name = prefix + "write.busy";
  temp.desc = "Total busy time for write request";
  list.push_back(temp);

  temp.name = prefix + "request_count";
  temp.desc = "Total request count";
  list.push_back(temp);

  temp.name = prefix + "bytes";
  temp.desc = "Total data size in byte";
  list.push_back(temp);

  temp.name = prefix + "pages";
  temp.desc = "Total request in page";
  list.push_back(temp);

  temp.name = prefix + "busy";
  temp.desc = "Total busy time";
  list.push_back(temp);
}

void SimpleDRAM::getStatValues(std::vector<double> &values) {
  AbstractDRAM::getStatValues(values);

  values.push_back(readStat.count);
  values.push_back(readStat.size);
  values.push_back(readStat.page);
  values.push_back(readStat.tick);
  values.push_back(writeStat.count);
  values.push_back(writeStat.size);
  values.push_back(writeStat.page);
  values.push_back(writeStat.tick);
  values.push_back(readStat.count + writeStat.count);
  values.push_back(readStat.size + writeStat.size);
  values.push_back(readStat.page + writeStat.page);
  values.push_back(readStat.tick + writeStat.tick);
}

void SimpleDRAM::resetStatValues() {
  AbstractDRAM::resetStatValues();

  readStat = Stat();
  writeStat = Stat();
}

}  // namespace DRAM

}  // namespace SimpleSSD
