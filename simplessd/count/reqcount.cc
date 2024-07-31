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

#include "reqcount.hh"

#include "string.h"
#include "util/algorithm.hh"
#include "sim/trace.hh"

namespace SimpleSSD{

namespace Count{

ReqCount* readreqcount = nullptr;
ReqCount* writereqcount = nullptr;

ReqCount::ReqCount(ConfigReader &c):conf(c){
  seqreq = 0;
  seqlength = 0;

  randreq = 0;
  randlength = 0;

  lastend = 0;
  lastlength = 0;
  
  isfirst = true;

  use = conf.readBoolean(CONFIG_COUNT, COUNT_REQCOUNT);
}

ReqCount::~ReqCount(){
  seqreq = 0;
  seqlength = 0;

  randreq = 0;
  randlength = 0;

  lastend = 0;
  
  isfirst = true;
}

void ReqCount::checkseq(uint64_t begin, uint64_t length){
    if(!use)
      return;
    if(isfirst){
        lastlength = length;
        isfirst = false; 
    }
    else if(lastend == begin){
        seqreq++;
        seqlength += length;
        if(lastlength != 0){
          seqreq++;
          seqlength += lastlength;
          lastlength = 0;
        }
    }
    else{
        if(lastlength != 0){
          randreq++;
          randlength += lastlength;
        }
        isfirst = false;
        lastlength = length;
    }
    lastend = begin + length;

}

void InitReqCount(ConfigReader & c){
    readreqcount = new ReqCount(c);
    writereqcount = new ReqCount(c);
}

void CheckReadSeq(uint64_t begin, uint64_t length){
    if(readreqcount == nullptr)
      return;
    readreqcount->checkseq(begin, length);
}

void CheckWriteSeq(uint64_t begin, uint64_t length){
    if(writereqcount == nullptr)
      return;
    writereqcount->checkseq(begin, length);
}

void deInitReqCount(){
  if(readreqcount){
    delete readreqcount;
    readreqcount = nullptr;
  }
  if(writereqcount){
    delete writereqcount;
    writereqcount = nullptr;
  }
}

void printReqCount(){
    CheckReadSeq(0, 0); 
    CheckWriteSeq(0, 0);
    if(!readreqcount->use){
      debugprint(LOG_REQCOUNT, "ReqCount is closed");
      return;
    }
        if(readreqcount != nullptr){
        debugprint(LOG_REQCOUNT, "Total read request:%u, sequential read request:%u, random read request:%u"
                    ,readreqcount->seqreq + readreqcount->randreq, readreqcount->seqreq, readreqcount->randreq);
        if(readreqcount->seqreq + readreqcount->randreq){
          debugprint(LOG_REQCOUNT, "Sequential read request ratio %lf | Random read request ratio %lf",
                    (double)readreqcount->seqreq / (readreqcount->seqreq + readreqcount->randreq ),
                    (double)readreqcount->randreq / (readreqcount->seqreq + readreqcount->randreq));}
        debugprint(LOG_REQCOUNT, "Total read data volume:%" PRIu64 ", sequential read data volume:%" PRIu64 ", random read data volume:%" PRIu64 ""
                    ,readreqcount->seqlength + readreqcount->randlength, readreqcount->seqlength, readreqcount->randlength);
        if(readreqcount->seqlength + readreqcount->randlength){
          debugprint(LOG_REQCOUNT, "Sequential read volume ratio %lf | Random read volume ratio %lf",
                    (double)readreqcount->seqlength / (readreqcount->seqlength + readreqcount->randlength),
                    (double)readreqcount->randlength / (readreqcount->seqlength + readreqcount->randlength));}
    }
    if(writereqcount != nullptr){
        debugprint(LOG_REQCOUNT, "Total write request:%u, sequential write request:%u, random write request:%u"
                    ,writereqcount->seqreq + writereqcount->randreq, writereqcount->seqreq, writereqcount->randreq);
        if(writereqcount->seqreq + writereqcount->randreq){
          debugprint(LOG_REQCOUNT, "Sequential write request ratio %lf | Random write request ratio %lf",
                    (double)writereqcount->seqreq / (writereqcount->seqreq + writereqcount->randreq),
                    (double)writereqcount->randreq / (writereqcount->seqreq + writereqcount->randreq));}
        debugprint(LOG_REQCOUNT, "Total write data volume:%" PRIu64 ", sequential write data volume:%" PRIu64 ", random write data volume:%" PRIu64 ""
                    ,writereqcount->seqlength + writereqcount->randlength, writereqcount->seqlength, writereqcount->randlength);
        if(writereqcount->seqlength + writereqcount->randlength){
          debugprint(LOG_REQCOUNT, "Sequential write volume ratio %lf | Random write volume ratio %lf",
                    (double)writereqcount->seqlength / (writereqcount->seqlength + writereqcount->randlength),
                    (double)writereqcount->randlength / (writereqcount->seqlength + writereqcount->randlength));}
    }

}

}

}