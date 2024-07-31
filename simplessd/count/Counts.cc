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
 * Authors: Jie Zhang <jie@camelab.org>
 */

#include "Counts.hh"

#include <string.h>

#include "util/algorithm.hh"
#include "sim/trace.hh"

namespace SimpleSSD{

namespace Count{
OpCount *opcount = nullptr;

OpCount::OpCount(ConfigReader & c):channelcount(nullptr), diecount(nullptr),blockcount(nullptr),pagecount(nullptr), conf(c){
  use = conf.readBoolean(CONFIG_COUNT, COUNT_OPCOUNT);
  recordC = conf.readBoolean(CONFIG_COUNT, COUNT_RECORDCHANNEL);
  recordD = conf.readBoolean(CONFIG_COUNT, COUNT_RECORDDIE);
  recordB = conf.readBoolean(CONFIG_COUNT, COUNT_RECORDBLOCK);
  recordP = conf.readBoolean(CONFIG_COUNT, COUNT_RECORDPAGE);

}
OpCount::~OpCount(){
    if(channelcount){
      for(uint i = 0; i < 2; i ++){
        if(channelcount[i]){
          delete[] channelcount[i];
          channelcount[i] = nullptr;
        }
      }
      delete channelcount;
      channelcount = nullptr;
    }

    if(diecount){
      for(uint i = 0; i < 2; i ++){
        if(diecount[i]){
          delete[] diecount[i];
          diecount[i] = nullptr;
        }
      }
      delete diecount;
      diecount = nullptr;
    }
    if(blockcount){
      for(uint i = 0; i < 2; i ++){
        if(blockcount[i]){
          delete[] blockcount[i];
          blockcount[i] = nullptr;
        }
      }
      delete blockcount;
      blockcount = nullptr;
    }

    if(pagecount){
      for(uint i = 0; i < 2; i ++){
        if(pagecount[i]){
          delete[] pagecount[i];
          pagecount[i] = nullptr;
        }
      }
      delete pagecount;
      pagecount = nullptr;
    }
}

void OpCount::SetOpCount(uint32_t nchannel, uint32_t npackage, uint32_t ndie, uint32_t nplane, uint32_t nblock, 
  uint32_t npage) {
    channel = nchannel;
    package = npackage;
    die = ndie;
    plane = nplane;
    block = nblock;
    page = npage;
    
    if(!this->use){
      debugprint(LOG_OPCOUNT, "OpCount is close");
      opprint("OpCount is close");
      return;
    }
    uint32_t objectnum = nchannel;
    if(recordC){
      channelcount = new uint32_t* [2]();
      channelcount[0] = new uint32_t[objectnum]();
      memset(channelcount[0], 0, objectnum * sizeof(uint32_t));
      channelcount[1] = new uint32_t[objectnum]();
      memset(channelcount[1], 0, objectnum * sizeof(uint32_t));
    }

    objectnum *= package * die;
    if(recordD){
      diecount = new uint32_t* [2]();
      diecount[0] = new uint32_t[objectnum]();
      memset(diecount[0], 0, objectnum * sizeof(uint32_t));
      diecount[1] = new uint32_t[objectnum]();
      memset(diecount[1], 0, objectnum * sizeof(uint32_t));
    }

    objectnum *= plane * block;
    if(recordB){
      blockcount = new uint32_t* [2]();
      blockcount[0] = new uint32_t[objectnum]();
      memset(blockcount[0], 0, objectnum * sizeof(uint32_t));
      blockcount[1] = new uint32_t[objectnum](); 
      memset(blockcount[1], 0, objectnum * sizeof(uint32_t));
    }

    objectnum *= page;
    if(recordP){
      pagecount = new uint32_t* [2]();
      pagecount[0] = new uint32_t[objectnum]();
      memset(pagecount[0], 0, objectnum * sizeof(uint32_t));
      pagecount[1] = new uint32_t[objectnum]();
      memset(pagecount[1], 0, objectnum * sizeof(uint32_t));
    }
    
}


void OpCount::AddCount(CPDPBP* pCPDPBP, PAL_OPERATION op){
  if(!use)
    return;
  uint32_t nchannel = pCPDPBP->Channel;
  uint32_t ndie = nchannel * package * die + pCPDPBP->Die + pCPDPBP->Package * die;
  uint32_t nblock = ndie * plane * block + pCPDPBP->Block + pCPDPBP->Plane * block;
  uint32_t npage = nblock * page + pCPDPBP->Page;
  //opprint(LOG_OPCOUNT,"add channel %u | die %u | block %u | page %u ", nchannel, ndie ,nblock, npage);
  if(recordC)
    channelcount[op][nchannel]++;
  if(recordD)
    diecount[op][ndie]++;
  if(recordB)
    blockcount[op][nblock]++;
  if(recordP)
    pagecount[op][npage]++;
}

uint32_t OpCount::CPDPBPToBlock(CPDPBP* pCPDPBP){
  uint32_t nchannel = pCPDPBP->Channel;
  uint32_t ndie = nchannel * package * die + pCPDPBP->Die + pCPDPBP->Package * die;
  uint32_t nblock = ndie * plane * block + pCPDPBP->Block + pCPDPBP->Plane * block;
  return nblock;
}

void InitOpCount(ConfigReader &c){
  if(opcount)
    delete opcount;
  opcount = new OpCount(c);
}

void SetOpCount(uint32_t channel, uint32_t package, uint32_t die, uint32_t plane, uint32_t block, 
  uint32_t page){
    opprint("Set with %u channel | %u package | %u die | %u plane | %u block | %u page", 
    channel, package, die, plane, block, page);
    opcount->SetOpCount(channel, package, die, plane, block, page);
}

void Addcount(CPDPBP* pCPD, PAL_OPERATION op){
  if(opcount == nullptr)
    return;
  debugprint(LOG_OPCOUNT, "Add channel %u | package %u | die %u | plane %u | block %u | page %u | Operation %u", pCPD->Channel
  , pCPD->Package, pCPD->Die, pCPD->Plane, pCPD->Block, pCPD->Page, op);
  opcount->AddCount(pCPD, op);
}

uint32_t CPDPBP2Block(CPDPBP* pCPD){
  if(opcount == nullptr)
    return 0;
  return opcount->CPDPBPToBlock(pCPD);
}

void deInitOpCount(){
  delete opcount;
  opcount = nullptr;
}


uint32_t BlockToChannel(uint32_t nblock){
  return nblock / (opcount->package * opcount->die * opcount->plane * opcount->block);
}

uint32_t BlockToDie(uint32_t nblock){
  nblock -= BlockToChannel(nblock) * (opcount->package * opcount->die * opcount->plane * opcount->block);
  return nblock / (opcount->plane * opcount->block);
}

uint32_t BlockToPlane(uint32_t nblock){
  nblock = nblock % (opcount->plane * opcount->block);
  nblock = nblock / opcount->block;
  return nblock;
}

uint32_t BlockToblock(uint32_t nblock){
  return nblock % (opcount->block);
}

void OpCount::printout(){
  if(!use)
    return;
  opprint("OpCount output:");
  uint32_t objectnum = channel;
  if(recordC){
    for(uint i = 0; i < objectnum; i++){
      if(opcount->channelcount[0][i])
        opprint("Channel %u read count %u", i, opcount->channelcount[0][i]);
    }
  }
  objectnum *= package * die;
  if(recordD){
    for(uint i = 0; i < objectnum; i++){
      if(opcount->diecount[0][i])
        opprint("Channel %u | Die %u read count %u", i / (package * die), i % (package * die), opcount->diecount[0][i]);
    }
  }
  objectnum *= plane * block;
  if(recordB){
    for(uint i = 0; i < objectnum; i++){
      if(opcount->blockcount[0][i])
        opprint("Channel %u | Die %u | Plane %u | Block %u read count %u", 
        BlockToChannel(i), BlockToDie(i), BlockToPlane(i), BlockToblock(i), opcount->blockcount[0][i]);
    }
  }
  objectnum *= page;
  if(recordP){
    for(uint i = 0; i < objectnum; i++){
      if(opcount->pagecount[0][i])
        opprint("Channel %u | Die %u | Plane %u | Block %u | Page %u read count %u", 
        BlockToChannel(i / page), BlockToDie(i / page), BlockToPlane(i / page), 
        BlockToblock(i/ page), i % page, opcount->pagecount[0][i]);
    }
  }

  objectnum = channel;
  if(recordC){
    for(uint i = 0; i < objectnum; i++){
      if(opcount->channelcount[1][i])
        opprint("Channel %u write count %u", i, opcount->channelcount[1][i]);
    }
  }
  objectnum *= package * die;
  if(recordD){
    for(uint i = 0; i < objectnum; i++){
      if(opcount->diecount[1][i])
        opprint("Channel %u | Die %u write count %u", i /(package * die), i % (package * die), opcount->diecount[1][i]);
    }
  }
  objectnum *= plane * block;
  if(recordB){
    for(uint i = 0; i < objectnum; i++){
      if(opcount->blockcount[1][i])
        opprint("Channel %u | Die %u | Plane %u | Block %u write count %u", 
        BlockToChannel(i), BlockToDie(i), BlockToPlane(i), BlockToblock(i), opcount->blockcount[1][i]);
    }
  }
  objectnum *= page;
  if(recordP){
    for(uint i = 0; i < objectnum; i++){
      if(opcount->pagecount[1][i])
        opprint("Channel %u | Die %u | Plane %u | Block %u | Page %u write count %u", 
        BlockToChannel(i / page), BlockToDie(i / page), BlockToPlane(i / page), 
        BlockToblock(i/ page), i % page, opcount->pagecount[1][i]);
    }
  }


}

void printOpCount(){
  if(opcount)
    opcount->printout();
}
}

}