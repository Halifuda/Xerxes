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

#include "count/config.hh"

#include "util/algorithm.hh"
#include "util/simplessd.hh"

#include <limits>

namespace SimpleSSD {

namespace Count{

const char NAME_BOPENBLOCK[] = "OpenBlockThreshold";
const char NAME_OPENBLOCK[] = "EnableOpenBlock";
const char NAME_REQCOUNT[] = "EnableReqCount";
const char NAME_OPCOUNT[] = "EnableOpCount";
const char NAME_RECORDCHANNEL[] = "RecordChannel";
const char NAME_RECORDDIE[] = "RecordDie";
const char NAME_RECORDBLOCK[] = "RecordBlock";
const char NAME_RECORDPAGE[] = "RecordPage";

Config::Config() {
  openblockthreshold = std::numeric_limits<uint64_t>::max();
  openblock = false;
  reqcount = false;
  opcount = false;
  recordchannel = false;
  recorddie = false;
  recordblock = false;
  recordpage = false;
}

bool Config::setConfig(const char *name, const char *value) {
  bool ret = true;

  if (MATCH_NAME(NAME_BOPENBLOCK)) {
    openblockthreshold = strtoul(value, nullptr, 10);
  }
  else if (MATCH_NAME(NAME_OPENBLOCK)){
    openblock = convertBool(value);
  }
  else if(MATCH_NAME(NAME_REQCOUNT)){
    reqcount = convertBool(value);
  }
  else if(MATCH_NAME(NAME_OPCOUNT)){
    opcount = convertBool(value);
  }
  else if(MATCH_NAME(NAME_RECORDCHANNEL)){
    recordchannel = convertBool(value);
  }
  else if(MATCH_NAME(NAME_RECORDDIE)){
    recorddie = convertBool(value);
  }
  else if(MATCH_NAME(NAME_RECORDBLOCK)){
    recordblock = convertBool(value);
  }
  else if(MATCH_NAME(NAME_RECORDPAGE)){
    recordpage = convertBool(value);
  }
  else {
    ret = false;
  }

  return ret;
}


void Config::update() {
  return;
}

uint64_t Config::readUint(uint32_t idx) {
  uint64_t ret = 0;

  switch (idx) {
    case COUNT_BOPENBLOCK:
      ret = openblockthreshold;
      break;
  }

  return ret;
}

bool Config::readBoolean(uint32_t idx) {
  bool ret = false;

  switch (idx) {
    case COUNT_OPENBLOCK:
      ret = openblock;
      break;
    case COUNT_REQCOUNT:
      ret = reqcount;
      break;
    case COUNT_OPCOUNT:
      ret = opcount;
      break;
    case COUNT_RECORDCHANNEL:
      ret = recordchannel;
      break;
    case COUNT_RECORDDIE:
      ret = recorddie;
      break;
    case COUNT_RECORDBLOCK:
      ret = recordblock;
      break;
    case COUNT_RECORDPAGE:
      ret = recordpage;
      break;
    
  }

  return ret;
}
}

}  // namespace SimpleSSD
