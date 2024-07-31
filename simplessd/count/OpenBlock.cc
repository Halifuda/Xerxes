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

#include "OpenBlock.hh"

#include <unordered_map>
#include <list>

#include "string.h"
#include "util/algorithm.hh"
#include "sim/trace.hh"
#include "Counts.hh"
#include "count/config.hh"

namespace SimpleSSD{

namespace Count{

Openb* openb = nullptr;

OpenInterval::OpenInterval(uint64_t begin1, uint64_t end1, uint64_t begin2, uint64_t end2): 
        begintick1(begin1), endtick1(end1), begintick2(begin2), endtick2(end2){};


void OpenBlock::checkinterval(uint64_t begin, uint64_t end, uint64_t threshrod){
    if(begin - lastbegin > threshrod  && lastbegin){
        auto iter = OBlist.begin();
        while(iter != OBlist.end()){
            if(iter->begintick1 > begin){
                break;
            }
            iter++;
        }
        OBlist.insert(iter, OpenInterval(begin, end, lastbegin, lastend));
    }
    lastbegin = begin;
}

uint64_t OpenBlock::size(){
    return OBlist.size();
}

Openb::Openb(ConfigReader &c):table(nullptr), conf(c), totalblock(0){
    RecordThreshrod = conf.readUint(CONFIG_COUNT, COUNT_BOPENBLOCK);
    ifuse = conf.readBoolean(CONFIG_COUNT, COUNT_OPENBLOCK);
}

Openb::~Openb(){
    if(table){
        for(uint i = 0; i < totalblock; i++){
            if(table[i]){
                delete table[i];
                table[i] = nullptr;
            }
        }
        delete table;
        table = nullptr;
    }
}

void Openb::settable(uint32_t nblock){
    if(ifuse == false){
        debugprint(LOG_OPENBLOCK, "Don't record OpenBlock");
        obprint("OpenBlock record is close");
        return;
    }
    table = new OpenBlock* [nblock]();
    totalblock = nblock;
    memset(table, 0, nblock * sizeof(OpenBlock*));
}

void Openb::insert(uint32_t block, uint64_t begintick, uint64_t endtick){
    if(!table)
      return;
    if(!table[block]){
        table[block] = new OpenBlock();
        table[block]->lastbegin = 0;
    }
    table[block]->checkinterval(begintick, endtick, RecordThreshrod);
}

void InitOpenBlock(ConfigReader &conf){
    if(openb)
        delete openb;
    openb = new Openb(conf);
}

void Settable(uint32_t nblock){
    if(openb){
        openb->settable(nblock);
    }
}

void deInitOpenBlock(){
    if(openb){
        delete openb;
        openb = nullptr;
    }
}

void InsertOpenBlock(uint32_t block, uint64_t begintick, uint64_t endtick){
    if(openb == nullptr)
      return;
    openb->insert(block, begintick, endtick);
}

void printOpenBlock(){
    if(openb){
        openb->printout();
    }
}
void Openb::printout(){
    obprint("OpenBlock Output begin");
    if(!table){
        obprint("OpenBlock Output ending");
        return;
    }
    for(uint i = 0; i < openb->totalblock; i++){
        if(openb->table[i] && openb->table[i]->size()){
            uint count = 0;
            auto iter = openb->table[i]->OBlist.begin();
            auto listend = openb->table[i]->OBlist.end();
            while(iter != listend){
                obprint("Channel %u | Die %u | Plane %u | Block %u | %uth : begintick1: %" PRIu64 "  begintick2: %" PRIu64 " (%" PRIu64 ")", 
                BlockToChannel(i), BlockToDie(i), BlockToPlane(i), BlockToblock(i),count, iter->begintick2, 
                iter->begintick1, iter->begintick1 - iter->begintick2);
                iter++;
                count++;
            }
        }
    }
    obprint("OpenBlock Output end");
}

}

}