#include "PALa_Statistics.h"

#include "sim/simulator.hh"

void ChannelStatistics::updateSuspend(PAL_OPERATION oper, CPDPBP addr) {
  SuspendStat *p = nullptr;
  if (oper == OPER_WRITE) {
    p = &write_suspend;
  } else if (oper == OPER_ERASE) {
    p = &erase_suspend;
    addr.Page = 0;
  } else {
    return;
  }
  if (p->count(addr) > 0) {
    p->at(addr)++;
  } else {
    p->insert(std::make_pair(addr, 1));
  }
}
uint64_t ChannelStatistics::getSuspend(PAL_OPERATION oper, CPDPBP addr) {
  SuspendStat *p = nullptr;
  if (oper == OPER_WRITE) {
    p = &write_suspend;
  } else if (oper == OPER_ERASE) {
    p = &erase_suspend;
    addr.Page = 0;
  } else {
    return 0;
  }
  if (p->count(addr) > 0) {
    return p->at(addr);
  } else {
    return 0;
  }
}

void ChannelStatistics::getStatList(std::vector<SimpleSSD::Stats> &list,
                                    std::string prefix) {
  SimpleSSD::Stats temp;
  
  // BUG: EnableSCA = 0时，cabus其实统计的是databus
  temp.name = prefix + "cabus_busy_ratio";
  temp.desc = "Busy time ratio over elapsed time of Cmd/Addr bus";
  list.push_back(temp);

  temp.name = prefix + "data_bus_busy_ratio";
  temp.desc = "Busy time ratio over elapsed time of Data bus";
  list.push_back(temp);

  temp.name = prefix + "die_busy_ratio";
  temp.desc = "Busy time ratio over elapsed time of dies";
  list.push_back(temp);

  temp.name = prefix + "energy.read";
  temp.desc = "Consumed energy of NAND read operation (uJ)";
  list.push_back(temp);

  temp.name = prefix + "energy.program";
  temp.desc = "Consumed energy of NAND program operation (uJ)";
  list.push_back(temp);

  temp.name = prefix + "energy.erase";
  temp.desc = "Consumed energy of NAND erase operation (uJ)";
  list.push_back(temp);

  temp.name = prefix + "read.count";
  temp.desc = "Total read operation count";
  list.push_back(temp);

  temp.name = prefix + "read.cmdaddrwait";
  temp.desc = "Average time of read waiting on cmd/addr";
  list.push_back(temp);

  temp.name = prefix + "read.cmdaddr";
  temp.desc = "Average time of read on cmd/addr";
  list.push_back(temp);

  temp.name = prefix + "read.flashwait";
  temp.desc = "Average time of read waiting on flash";
  list.push_back(temp);

  temp.name = prefix + "read.flash";
  temp.desc = "Average time of read on flash";
  list.push_back(temp);

  temp.name = prefix + "read.doutwait";
  temp.desc = "Average time of read waiting on data out";
  list.push_back(temp);

  temp.name = prefix + "read.dout";
  temp.desc = "Average time of read on dataout";
  list.push_back(temp);

  temp.name = prefix + "program.count";
  temp.desc = "Total program operation count";
  list.push_back(temp);

  temp.name = prefix + "program.cmdaddrwait";
  temp.desc = "Average time of program waiting on cmd/addr";
  list.push_back(temp);

  temp.name = prefix + "program.cmdaddr";
  temp.desc = "Average time of program on cmd/addr";
  list.push_back(temp);

  temp.name = prefix + "program.dinwait";
  temp.desc = "Average time of program waiting on data in";
  list.push_back(temp);

  temp.name = prefix + "program.din";
  temp.desc = "Average time of program on data in";
  list.push_back(temp);

  temp.name = prefix + "program.flashwait";
  temp.desc = "Average time of program waiting on flash";
  list.push_back(temp);

  temp.name = prefix + "program.flash";
  temp.desc = "Average time of program on flash";
  list.push_back(temp);

  temp.name = prefix + "erase.count";
  temp.desc = "Total erase operation count";
  list.push_back(temp);

  temp.name = prefix + "erase.cmdaddrwait";
  temp.desc = "Average time of erase waiting on cmd/addr";
  list.push_back(temp);

  temp.name = prefix + "erase.cmdaddr";
  temp.desc = "Average time of erase on cmd/addr";
  list.push_back(temp);

  temp.name = prefix + "erase.flashwait";
  temp.desc = "Average time of erase waiting on flash";
  list.push_back(temp);

  temp.name = prefix + "erase.flash";
  temp.desc = "Average time of erase on flash";
  list.push_back(temp);
}
void ChannelStatistics::getStatValues(std::vector<double> &values) {
  double elapsed = SimpleSSD::getTick() - last_reset_tick;
  // ca bus busy ratio
  values.push_back((double)cabusy / elapsed);
  // data bus busy ratio
  values.push_back((double)dbusy / elapsed);
  // die busy ratio
  values.push_back((double)dietotalbusy / numDies / elapsed);
  // energy.read
  values.push_back(read.energy);
  // energy.program
  values.push_back(write.energy);
  // energy.erase
  values.push_back(erase.energy);
  // read.count
  values.push_back(read.count);
  // read.cmdaddrwait
  values.push_back(read.cmdaddrwait);
  // read.cmdaddr
  values.push_back(read.cmdaddr);
  // read.flashwait
  values.push_back(read.flashwait);
  // read.flash
  values.push_back(read.flash);
  // read.doutwait
  values.push_back(read.datawait);
  // read.dout
  values.push_back(read.data);
  // program.count
  values.push_back(write.count);
  // program.cmdaddrwait
  values.push_back(write.cmdaddrwait);
  // program.cmdaddr
  values.push_back(write.cmdaddr);
  // program.dinwait
  values.push_back(write.datawait);
  // program.din
  values.push_back(write.data);
  // program.flashwait
  values.push_back(write.flashwait);
  // program.flash
  values.push_back(write.flash);
  // erase.count
  values.push_back(erase.count);
  // erase.cmdaddrwait
  values.push_back(erase.cmdaddrwait);
  // erase.cmdaddr
  values.push_back(erase.cmdaddr);
  // erase.flashwait
  values.push_back(erase.flashwait);
  // erase.flash
  values.push_back(erase.flash);
}

void ChannelStatistics::reset() {
    last_reset_tick = SimpleSSD::getTick();
    cabusy = dbusy = dietotalbusy = 0;
    read.reset();
    write.reset();
    erase.reset();
    // donot reset suspend
}