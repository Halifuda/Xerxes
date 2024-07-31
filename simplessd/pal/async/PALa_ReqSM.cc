#include "PALa_ReqSM.h"

#include "sim/trace.hh"

#include <cstring>


#include <unistd.h>       // for syscall()
#include <sys/syscall.h>

/** ---------- State Instances ---------- */

namespace RequestStates {

static ChannelEnable channel_enable;
static CmdAddrIn cmdaddr_in;
static DataIn data_in;
static FlashRead flash_read;
static FlashProgram flash_program;
static FlashErase flash_erase;
static DataOut data_out;
static Ending _ending;
static BaseState *end_state = &_ending;
// AYDTODO: temp code
static ChannelDMA0 ch_dma0;
static ChannelDMA1 ch_dma1;

}  // namespace RequestStates

/** ---------- PALRequestSM ---------- */

bool PALRequestSM::checkSuspend() {
  if (cmd.operation != OPER_WRITE && cmd.operation != OPER_ERASE) {
    return false;
  }
  auto curtime = chstats->getSuspend(cmd.operation, relatedModules.addr);
  return curtime < maxSuspend;
}

void PALRequestSM::doSuspend(aTimeSlot newslot) {
  suspendTime++;
  curTimeSlot = newslot;
  chstats->updateSuspend(cmd.operation, relatedModules.addr);
  SimpleSSD::deschedule(curEvent);
  SimpleSSD::schedule(curEvent, newslot.end);
  if (cmd.operation == OPER_WRITE) {
    SimpleSSD::debugprint(
        SimpleSSD::LOG_PAL_ASYNC,
        "Current program suspension times at %u-%u-%u-%u-%u-%u: %lu",
        relatedModules.addr.Channel, relatedModules.addr.Package,
        relatedModules.addr.Die, relatedModules.addr.Plane,
        relatedModules.addr.Block, relatedModules.addr.Page,
        chstats->getSuspend(cmd.operation, relatedModules.addr));
  } else {
    SimpleSSD::debugprint(
        SimpleSSD::LOG_PAL_ASYNC,
        "Current erase suspension times at %u-%u-%u-%u-%u: %lu",
        relatedModules.addr.Channel, relatedModules.addr.Package,
        relatedModules.addr.Die, relatedModules.addr.Plane,
        relatedModules.addr.Block,
        chstats->getSuspend(cmd.operation, relatedModules.addr));
  }
}

bool PALRequestSM::atFlashState() {
  if (curState == nullptr) {
    return false;
  }
  auto nextState = curState->nextState(cmd, relatedModules);
  return nextState.first == &RequestStates::flash_read ||
         nextState.first == &RequestStates::flash_program ||
         nextState.first == &RequestStates::flash_erase;
}


bool PALRequestSM::ending() { return curState == RequestStates::end_state; }

void PALRequestSM::next(uint64_t tick) {
  BaseState *nextState = nullptr;
  if (curState == nullptr) {
    nextState = &RequestStates::channel_enable;
    curSched = relatedModules.channel;
  }
  // Not starting.
  else {
    nextState = curState->trans(cmd, relatedModules, &curSched);
    if (nextState == &RequestStates::flash_erase ||
        nextState == &RequestStates::flash_program) {
      // first enter P/E, set suspention callback
      auto pDie = static_cast<Die *>(curSched);
      pDie->setSuspendCallbacks(
          [this]() { return checkSuspend(); },
          [this](aTimeSlot newslot) { doSuspend(newslot); });
    }
    else if (curState == &RequestStates::flash_erase ||
             curState == &RequestStates::flash_program) {
      // quit P/E, reset suspension callback
      /* auto pDie = relatedModules.die;
      pDie->setSuspendCallbacks(
        []() { return false; },
        [](aTimeSlot) { return; }
      );*/
    }
  }
  
  aTimeSlot newSlot{};
  // Not ending.
  if (nextState != RequestStates::end_state) {
    uint64_t bound = tick;
    // Strict time bound
    if (bound < cmd.arrived) bound = cmd.arrived;

    uint64_t latency = 
        nextState->finishingLatency(cmd, lat, relatedModules.addr);
    // Multiplane datain latency adjust.
    // AYDTODO: need CAin do this?

    

    if (nextState == &RequestStates::data_in && 
        cmd.operation == OPER_WRITE && 
        cmd.is_multiplane) {
      latency *= relatedModules.die->getPlaneCnt();
    }
    newSlot = curSched->allocate(cmd, relatedModules, bound, latency);

    if (!newSlot) {
      SimpleSSD::panic("Failed to allocate TimeSlot");
    }
    nextState->updateChannelStats(cmd, lat, chstats->getOperStat(cmd.operation),
                                  newSlot.start - curTimeSlot.end, latency);
    if (nextState == &RequestStates::flash_program &&
        cmd.operation == OPER_WRITE &&
        cmd.is_multiplane) {
      auto opst = chstats->getOperStat(cmd.operation);
      auto mul = relatedModules.die->getPlaneCnt() - 1;
      opst->energy += lat->GetPower(cmd.operation, BUSY_MEM) * latency * mul /
                      (double)1000000000;
    }
    curTimeSlot = newSlot;
  }
  curState = nextState;
  // std::cout<<id<<" "<<retry_num<<" "<<cmd.arrived<<std::endl;
  // SimpleSSD::retryprint("id: %u, retry_num: %u, cmd.arrived: %u", id, retry_num, cmd.arrived);
  if (curState != RequestStates::end_state) {
    curEvent = SimpleSSD::allocate([this](uint64_t t) {
      relatedModules.channel->schedNextState(t, this);
    });
    SimpleSSD::schedule(curEvent, curTimeSlot.end);
  } 
  else {
    if(retry->config.is_retry && cmd.operation == OPER_READ){
      CPDPBP addr = relatedModules.addr;
      uint32_t retention = retry->get_retention(addr.Block, id);
      bool flag = SimpleSSD::ERROR::IsOpenBlock(SimpleSSD::Count::CPDPBP2Block(&addr));
      curTimeSlot.end = curTimeSlot.end + retry->ldpc.get_latency(retry->get_ERROR(addr.Block, addr.Page, retry_num, flag, retention));
      
      

      if(retry_num == 0){
        // std::cout<<cmd.finished<<std::endl;
        arriveTick = cmd.arrived;
        start_ = curTimeSlot.end;
        SimpleSSD::retryprint("Channel %u | Package %u | Die %u | Plane %u | Block %u | Page %u | tick: %lu, retention: %s, default error: %u, reqID: %lu", 
              addr.Channel, addr.Package, addr.Die, addr.Plane, addr.Block, addr.Page, cmd.arrived, retry->config.retentions_map[retention].c_str(), retry->get_ERROR(addr.Block, addr.Page, 0, 0, retention), id);
        SimpleSSD::retryprint(" ");
      }
      if(retry->is_Retry(addr, retry_num, flag, retention)){
        retry_num++;
        std::vector<SimpleSSD::ERROR::voltage> v = retry->config.get_voltage(addr.Page);
        SimpleSSD::ERROR::RETRYTABLE retry_table = retry->config.get_retry_table(flag);
        if(retry_table[v[0]].size() < retry_num){
          retry->retryStatistics.closeRetry(addr,curTimeSlot.end,start_,false);
          
          commitCallback(tick, id);
        }
        else{
          std::string str= "";
          for(uint32_t j=0;j<v.size();j++){
                // std::cout<<"R"<< v[j] + 1 <<":"<<retry_table[v[j]][i]<<"   ";
                str += "R" + std::to_string(v[j] + 1) + ":" + std::to_string(retry_table[v[j]][retry_num - 1])+ " ";
          }
          if(flag){
                retry->count_open++;
                // std::cout<<"OPEN"<<std::endl;
                str += "OPEN";
          }else{
                retry->count_close++;
                // std::cout<<"CLOSE"<<std::endl;
                str += "CLOSE";
          }

          retry->retryStatistics.addRetry(addr, cmd.finished);

          cmd.arrived = curTimeSlot.end;
          // cmd.finished = 0;
          SimpleSSD::retryprint("Channel %u | Package %u | Die %u | Plane %u | Block %u | Page %u | tick: %lu, retention: %s, retry %u: %s error: %u", 
                addr.Channel, addr.Package, addr.Die, addr.Plane, addr.Block, addr.Page, cmd.arrived, retry->config.retentions_map[retention].c_str(), retry_num, str.c_str(), retry->get_ERROR(addr.Block, addr.Page, retry_num, flag, retention));
          SimpleSSD::retryprint(" ");
          curState = nullptr;
          curEvent = SimpleSSD::allocate([this](uint64_t t) {
            relatedModules.channel->schedNextState(t, this);
          });
          SimpleSSD::schedule(curEvent, curTimeSlot.end);

        }
      }
      else{
        retry->retryStatistics.closeRetry(addr,curTimeSlot.end,start_,true);
        cmd.arrived = arriveTick;
        // SimpleSSD::retryprint("2-1 ");
        commitCallback(tick, id);
      }
    }
    else{
      commitCallback(tick, id);
    }

  }
  // Debug output
  char name[15];
  bool debugoutput = true;
  if (curState == &RequestStates::ch_dma0) {
    strcpy(name, "Cmd/AddrIn");
  } else if (curState == &RequestStates::data_in) {
    strcpy(name, "DataIn");
    if(cmd.operation == OPER_WRITE) {
      cmd.memstart = curTimeSlot.start;
    }
  } else if (curState == &RequestStates::flash_read) {
    strcpy(name, "FlashRead");
  } else if (curState == &RequestStates::flash_program) {
    strcpy(name, "FlashProgram");
  } else if (curState == &RequestStates::flash_erase) {
    strcpy(name, "FlashErase");
  } else if (curState == &RequestStates::data_out) {
    strcpy(name, "DataOut");
  } else if (curState == &RequestStates::ch_dma1) {
    strcpy(name, "CmdOut");
    if(cmd.operation == OPER_WRITE) {
      cmd.memend = curTimeSlot.end;
      CPDPBP addr = relatedModules.addr;
      SimpleSSD::Count::InsertOpenBlock(SimpleSSD::Count::CPDPBP2Block(&addr),cmd.memstart, cmd.memend);
    }
  } else {
    debugoutput = false;
  }
  if (debugoutput) {
    SimpleSSD::debugprint(SimpleSSD::LOG_PAL_ASYNC,
                          "Request %lu allocated timeslot %lu-%lu(%lu) for %s",
                          id, curTimeSlot.start, curTimeSlot.end,
                          curTimeSlot.len(), name);
  }
}

void PALRequestSM::start() {
  curEvent = SimpleSSD::allocate(
      [this](uint64_t t) { relatedModules.channel->schedNextState(t, this); });
  SimpleSSD::schedule(curEvent, SimpleSSD::getTick() + 10);
}

/** ---------- BaseState ---------- */

BaseState *BaseState::trans(const Command &cmd, RelatedModules &modules,
                            BaseScheduleModule **ppSched) {
  auto pair = nextState(cmd, modules);
  *ppSched = pair.second;
  return pair.first;
}

namespace RequestStates {

/** ---------- Transition Methods ---------- */

BaseState::NextStateInfo ChannelEnable::nextState(const Command &,
                                                  RelatedModules &m) {
  NextStateInfo pair(&cmdaddr_in, m.channel->getCmdAddrBus());
  return pair;
}

uint64_t ChannelEnable::finishingLatency(const Command &, Latency *, CPDPBP &) {
  // AYDTODO: latency
  return 10;
}

void ChannelEnable::updateChannelStats(const Command &, Latency *, OperStat *,
                                       uint64_t, uint64_t) {}

BaseState::NextStateInfo CmdAddrIn::nextState(const Command &cmd,
                                              RelatedModules &m) {
  NextStateInfo pair(nullptr, nullptr);
  switch (cmd.operation) {
    case PAL_OPERATION::OPER_WRITE:
      pair.first = &data_in;
      pair.second = m.channel->getDataBus();
      break;
    // AYDTODO: temp code
    case PAL_OPERATION::OPER_READ:
    case PAL_OPERATION::OPER_ERASE:
      pair.first = &ch_dma0;
      pair.second = m.channel->getCmdAddrBus();
      break;
    default:
      SimpleSSD::panic(
          "PALasync: Request State Machine met unexpected PAL_OPERATION %u at "
          "AddrIn",
          cmd.operation);
      break;
  }
  return pair;
}

uint64_t CmdAddrIn::finishingLatency(const Command &, Latency *, CPDPBP &) {
  // AYDTODO: cmd/addr in latency now is calculated in DMA0
  // for old latency campatibility.
  // AYDTODO: need multi-plane request multiply CA latency?
  return 10;
}

void CmdAddrIn::updateChannelStats(const Command &, Latency *, OperStat *,
                                   uint64_t, uint64_t) {}

BaseState::NextStateInfo DataIn::nextState(const Command &, RelatedModules &m) {
  NextStateInfo pair(&flash_program, m.die);
  return pair;
}

uint64_t DataIn::finishingLatency(const Command &cmd, Latency *l,
                                  CPDPBP &addr) {
  if (cmd.operation != OPER_WRITE) {
    SimpleSSD::panic("Unexpected cmd type reaches DataIn (should be WRITE)");
  }
  // Only WRITE uses DataIn
  if (l->useNewModel()) {
    return l->NewModelLatency(addr.Page, OPER_WRITE, NEWLAT_DATAIN);
  }
  else {
    return l->GetLatency(addr.Page, OPER_WRITE, BUSY_DMA0);
  }
}

void DataIn::updateChannelStats(const Command &cmd, Latency *pl,
                                OperStat *opstat, uint64_t wait, uint64_t lat) {
  opstat->datawait += wait;
  opstat->data += lat;
  opstat->energy +=
      pl->GetPower(cmd.operation, BUSY_DMA0) * lat / (double)1000000000;
}

BaseState::NextStateInfo FlashRead::nextState(const Command &,
                                              RelatedModules &m) {
  NextStateInfo pair(&data_out, m.channel->getDataBus());
  return pair;
}

uint64_t FlashRead::finishingLatency(const Command &, Latency *l,
                                     CPDPBP &addr) {
  if (l->useNewModel()) {
    return l->NewModelLatency(addr.Page, OPER_READ, NEWLAT_READ);
  }
  return l->GetLatency(addr.Page, PAL_OPERATION::OPER_READ, BUSY_MEM);
}

void FlashRead::updateChannelStats(const Command &cmd, Latency *pl,
                                   OperStat *opstat, uint64_t wait,
                                   uint64_t lat) {
  opstat->flashwait += wait;
  opstat->flash += lat;
  opstat->count += 1;
  opstat->energy +=
      pl->GetPower(cmd.operation, BUSY_MEM) * lat / (double)1000000000;
}

BaseState::NextStateInfo FlashProgram::nextState(const Command &,
                                                 RelatedModules &m) {
  // AYDTODO: ch_dma1 is temp code
  NextStateInfo pair(&ch_dma1, m.channel->getCmdAddrBus());
  return pair;
}

uint64_t FlashProgram::finishingLatency(const Command &, Latency *l,
                                        CPDPBP &addr) {
  if (l->useNewModel()) {
    uint64_t iter = l->NewModelLatency(addr.Page, OPER_WRITE, NEWLAT_ISPP_ITER);
    uint64_t p = l->NewModelLatency(addr.Page, OPER_WRITE, NEWLAT_ISPP_PROGRAM);
    uint64_t v = l->NewModelLatency(addr.Page, OPER_WRITE, NEWLAT_ISPP_VERIFY);
    return iter * (p + v);
  }
  return l->GetLatency(addr.Page, PAL_OPERATION::OPER_WRITE, BUSY_MEM);
}

void FlashProgram::updateChannelStats(const Command &cmd, Latency *pl,
                                      OperStat *opstat, uint64_t wait,
                                      uint64_t lat) {
  opstat->flashwait += wait;
  opstat->flash += lat;
  opstat->count += 1;
  opstat->energy +=
      pl->GetPower(cmd.operation, BUSY_MEM) * lat / (double)1000000000;
}

BaseState::NextStateInfo FlashErase::nextState(const Command &,
                                               RelatedModules &m) {
  // AYDTODO: ch_dma1 is temp code
  NextStateInfo pair(&ch_dma1, m.channel->getCmdAddrBus());
  return pair;
}

uint64_t FlashErase::finishingLatency(const Command &, Latency *l,
                                      CPDPBP &addr) {
  if (l->useNewModel()) {
    uint64_t e = l->NewModelLatency(addr.Page, OPER_ERASE, NEWLAT_ERASE_PHASE);
    uint64_t v = l->NewModelLatency(addr.Page, OPER_ERASE, NEWLAT_ERASE_VERIFY);
    uint64_t r = l->NewModelLatency(addr.Page, OPER_ERASE, NEWLAT_ERASE_VRESET);
    return e + r + v + r;
  }
  return l->GetLatency(addr.Page, PAL_OPERATION::OPER_ERASE, BUSY_MEM);
}

void FlashErase::updateChannelStats(const Command &cmd, Latency *pl,
                                    OperStat *opstat, uint64_t wait,
                                    uint64_t lat) {
  opstat->flashwait += wait;
  opstat->flash += lat;
  opstat->count += 1;
  opstat->energy +=
      pl->GetPower(cmd.operation, BUSY_MEM) * lat / (double)1000000000;
}

BaseState::NextStateInfo DataOut::nextState(const Command &, RelatedModules &) {
  NextStateInfo pair(end_state, nullptr);
  return pair;
}

uint64_t DataOut::finishingLatency(const Command &cmd, Latency *l,
                                   CPDPBP &addr) {
  // Only READ uses DataOut
  if (cmd.operation != PAL_OPERATION::OPER_READ) {
    SimpleSSD::panic("Unexpected cmd type reaches DataOut (should be READ)");
  }
  if (l->useNewModel()) {
    return l->NewModelLatency(addr.Page, OPER_READ, NEWLAT_DATAOUT);
  }
  return l->GetLatency(addr.Page, PAL_OPERATION::OPER_READ, BUSY_DMA1);
}

void DataOut::updateChannelStats(const Command &cmd, Latency *pl,
                                 OperStat *opstat, uint64_t wait,
                                 uint64_t lat) {
  opstat->datawait += wait;
  opstat->data += lat;
  opstat->energy +=
      pl->GetPower(cmd.operation, BUSY_DMA1) * lat / (double)1000000000;
}

// AYDTODO: temp code
BaseState::NextStateInfo ChannelDMA0::nextState(const Command &cmd,
                                                RelatedModules &m) {
  NextStateInfo pair(nullptr, m.die);
  switch (cmd.operation) {
    case PAL_OPERATION::OPER_READ:
      pair.first = &flash_read;
      break;
    case PAL_OPERATION::OPER_ERASE:
      pair.first = &flash_erase;
      break;
    case PAL_OPERATION::OPER_WRITE:
      SimpleSSD::panic(
          "WRITE reaches ChannelDMA0 (it should reach DataIn instead)");
      break;
    default:
      SimpleSSD::panic("Unknown cmd type reaches ChannelDMA0");
      break;
  }
  return pair;
}

uint64_t ChannelDMA0::finishingLatency(const Command &cmd, Latency *l,
                                       CPDPBP &addr) {
  
  if (cmd.operation != PAL_OPERATION::OPER_READ &&
      cmd.operation != PAL_OPERATION::OPER_ERASE) {
    SimpleSSD::panic(
        "Unexpected cmd type reaches ChannelDMA0 (should be READ or ERASE)");
  }
  if (l->useNewModel()) {
    return l->NewModelLatency(addr.Page, cmd.operation, NEWLAT_CMDADDR);
  }
  return l->GetLatency(addr.Page, cmd.operation, BUSY_DMA0);
}

void ChannelDMA0::updateChannelStats(const Command &cmd, Latency *pl,
                                     OperStat *opstat, uint64_t wait,
                                     uint64_t lat) {
  opstat->cmdaddrwait += wait;
  opstat->cmdaddr += lat;
  opstat->energy +=
      pl->GetPower(cmd.operation, BUSY_DMA0) * lat / (double)1000000000;
}

BaseState::NextStateInfo ChannelDMA1::nextState(const Command &cmd,
                                                RelatedModules &) {
  if (cmd.operation != PAL_OPERATION::OPER_WRITE &&
      cmd.operation != PAL_OPERATION::OPER_ERASE) {
    SimpleSSD::panic(
        "Unexpected cmd type reaches ChannelDMA1 (should be WRITE or ERASE)");
  }
  NextStateInfo pair(end_state, nullptr);
  return pair;
}

uint64_t ChannelDMA1::finishingLatency(const Command &cmd, Latency *l,
                                       CPDPBP &addr) {
  if (cmd.operation != PAL_OPERATION::OPER_WRITE &&
      cmd.operation != PAL_OPERATION::OPER_ERASE) {
    SimpleSSD::panic(
        "Unexpected cmd type reaches ChannelDMA1 (should be WRITE or ERASE)");
  }
  if (l->useNewModel()) { 
    return l->NewModelLatency(addr.Page, cmd.operation, NEWLAT_DMA1);
  }
  return l->GetLatency(addr.Page, cmd.operation, BUSY_DMA1);
}

void ChannelDMA1::updateChannelStats(const Command &cmd, Latency *pl,
                                     OperStat *opstat, uint64_t wait,
                                     uint64_t lat) {
  opstat->cmdaddrwait += wait;
  opstat->cmdaddr += lat;
  opstat->energy +=
      pl->GetPower(cmd.operation, BUSY_DMA1) * lat / (double)1000000000;
}

}  // namespace RequestStates