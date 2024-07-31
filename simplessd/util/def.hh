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

#ifndef __UTIL_DEF__
#define __UTIL_DEF__

#include <cinttypes>
#include <cstddef>

#include "sim/dma_interface.hh"
#include "util/bitset.hh"

namespace SimpleSSD {

typedef struct _LPNRange {
  uint64_t slpn;
  uint64_t nlp;

  _LPNRange();
  _LPNRange(uint64_t, uint64_t);
} LPNRange;

typedef enum _RequestType {
  INVALID,
  READ,
  WRITE,
  TRIM,
  FLUSH,
  FORMAT,
  PALERASE,
} RequestType;

namespace HIL {

typedef struct _CallBackContext {
  void *context;
  bool readValidPage;

  _CallBackContext(void *);

} CallBackContext;

typedef struct _Request {
  RequestType reqType;
  uint64_t reqID;
  uint64_t reqSubID;
  uint64_t offset;
  uint64_t length;
  LPNRange range;

  uint64_t beginAt;
  uint64_t finishedAt;
  DMAFunction function;
  CallBackContext *context;
  bool none;

  _Request();
  _Request(DMAFunction &, void *);

  bool operator()(const _Request &a, const _Request &b);
} Request;

}  // namespace HIL

namespace ICL {
typedef struct _Request {
  RequestType reqType;
  uint64_t reqID;
  uint64_t reqSubID;
  uint64_t offset;
  uint64_t length;
  LPNRange range;
  uint32_t subIdx;
  bool readValidPage;
  bool internal;

  uint64_t beginAt;
  uint64_t finishAt;

  _Request();
  _Request(HIL::Request &);
} Request;

struct ReqKey {
  uint64_t id;
  uint64_t subid;
  ReqKey() : id(0), subid(0) {}
  ReqKey(uint64_t i, uint64_t s) : id(i), subid(s) {}
  ReqKey(const Request &r) : id(r.reqID), subid(r.reqSubID) {}
  bool operator==(const ReqKey &k) const {
    return id == k.id && subid == k.subid;
  }
};

struct ReqKeyHash {
  const static uint64_t seedA = 1177;
  const static uint64_t seedB = 7857;
  size_t operator()(const ReqKey &k) const {
    return k.id * seedA + k.subid * seedB;
  }
};
}  // namespace ICL

namespace FTL {

typedef struct _ReqInfo {
  uint64_t lpn;
  uint32_t idx;
  uint32_t subIdx;

  _ReqInfo();
  _ReqInfo(uint64_t, uint32_t, uint32_t);
} ReqInfo;

typedef struct _Request {
  RequestType reqType;
  uint64_t reqID;  // ID of ICL::Request
  uint64_t reqSubID;
  uint64_t ftlSubID;

  uint64_t beginAt;
  uint64_t finishAt;
  bool readValidPage;
  std::vector<ReqInfo> reqInfo;
  std::vector<ReqInfo> backInfo;

  _Request();
  _Request(uint32_t);
  _Request(ICL::Request &);
  _Request(uint32_t, uint32_t, ICL::Request &);
  void setLPN(uint64_t);
} Request;

}  // namespace FTL

namespace PAL {

typedef struct _Request {
  RequestType reqType;
  uint64_t reqID;  // ID of ICL::Request
  uint64_t reqSubID;
  uint64_t ftlSubID;  // ID of FTL::Request
  uint32_t blockIndex;
  uint32_t pageIndex;

  uint64_t beginAt;
  uint64_t finishAt;
  Bitset ioFlag;
  bool is_multiplane;
  bool coalesced;
  bool is_gc;

  _Request(uint32_t);
  _Request(uint32_t, FTL::Request &);
} Request;

}  // namespace PAL

}  // namespace SimpleSSD

#endif
