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

#include "util/def.hh"

#include <cstdlib>
#include <iostream>

namespace SimpleSSD {

LPNRange::_LPNRange() : slpn(0), nlp(0) {}

LPNRange::_LPNRange(uint64_t s, uint64_t n) : slpn(s), nlp(n) {}

namespace HIL {

CallBackContext::_CallBackContext(void *c)
    : context(c),
      readValidPage(true){}

Request::_Request()
    : reqType(INVALID),
      reqID(0),
      reqSubID(0),
      offset(0),
      length(0),
      beginAt(0),
      finishedAt(0),
      context(nullptr),
      none(false) {}

Request::_Request(DMAFunction &f, void *c)
    : reqType(INVALID),
      reqID(0),
      reqSubID(0),
      offset(0),
      length(0),
      beginAt(0),
      finishedAt(0),
      function(f),
      none(false) {
        context = new CallBackContext(c);
      }

bool Request::operator()(const Request &a, const Request &b) {
  return a.finishedAt > b.finishedAt;
}

}  // namespace HIL

namespace ICL {

Request::_Request()
    : reqType(INVALID),
      reqID(0),
      reqSubID(0),
      offset(0),
      length(0),
      subIdx(0),
      readValidPage(true),
      internal(false),
      beginAt(0),
      finishAt(0) {}

Request::_Request(HIL::Request &r)
    : reqType(r.reqType),
      reqID(r.reqID),
      reqSubID(r.reqSubID),
      offset(r.offset),
      length(r.length),
      range(r.range),
      readValidPage(true),
      internal(false),
      beginAt(r.beginAt),
      finishAt(r.finishedAt) {}

}  // namespace ICL

namespace FTL {

ReqInfo::_ReqInfo(): lpn(0), idx(0), subIdx(0){}

ReqInfo::_ReqInfo(uint64_t page, uint32_t iocount, uint32_t subPage)
    : lpn(page), idx(iocount), subIdx(subPage){}

Request::_Request()
    : reqType(INVALID),
      reqID(0),
      reqSubID(0),
      ftlSubID(0),
      beginAt(0),
      finishAt(0),
      readValidPage(true) {}

Request::_Request(uint32_t count)
    : reqType(INVALID),
      reqID(0),
      reqSubID(0),
      ftlSubID(0),
      beginAt(0),
      finishAt(0),
      readValidPage(true),
      reqInfo(count, ReqInfo(0, 0, 0)) {}

Request::_Request(ICL::Request &r)
    : reqType(r.reqType),
      reqID(r.reqID),
      reqSubID(r.reqSubID),
      ftlSubID(0),
      beginAt(r.beginAt),
      finishAt(r.finishAt),
      readValidPage(true) {
}

Request::_Request(uint32_t iocount, uint32_t subPage, ICL::Request &r)
    : reqType(r.reqType),
      reqID(r.reqID),
      reqSubID(r.reqSubID),
      ftlSubID(0),
      beginAt(r.beginAt),
      finishAt(r.finishAt) {
    uint64_t lpn = r.range.slpn / iocount;
    uint32_t idx = r.range.slpn % iocount;
    for(uint32_t i = 0; i < subPage; i++){
        reqInfo.push_back(ReqInfo(lpn, idx, i));
    }
}

void Request::setLPN(uint64_t lpn){
  for(uint32_t i = 0; i < reqInfo.size(); i++){
    reqInfo[i].lpn = lpn;
  }
}

}  // namespace FTL

namespace PAL {

Request::_Request(uint32_t iocount)
    : reqType(INVALID),
      reqID(0),
      reqSubID(0),
      ftlSubID(0),
      blockIndex(0),
      pageIndex(0),
      beginAt(0),
      finishAt(0),
      ioFlag(iocount),
      is_multiplane(false),
      coalesced(false),
      is_gc(false) {}

Request::_Request(uint32_t iocount, FTL::Request &r)
    : reqType(r.reqType),
      reqID(r.reqID),
      reqSubID(r.reqSubID),
      ftlSubID(r.ftlSubID), 
      blockIndex(0),
      pageIndex(0),
      beginAt(r.beginAt),
      finishAt(r.finishAt),
      ioFlag(iocount),
      is_multiplane(false),
      coalesced(false),
      is_gc(false) {}

}  // namespace PAL

}  // namespace SimpleSSD
