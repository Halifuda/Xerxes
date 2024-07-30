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

#ifndef __COUNT_CONFIG__
#define __COUNT_CONFIG__

#include "sim/base_config.hh"

namespace SimpleSSD {

namespace Count{
typedef enum {
  COUNT_BOPENBLOCK,
  COUNT_OPENBLOCK,
  COUNT_REQCOUNT,
  COUNT_OPCOUNT,
  COUNT_RECORDCHANNEL,
  COUNT_RECORDDIE,
  COUNT_RECORDBLOCK,
  COUNT_RECORDPAGE,
} COUNT_CONFIG;

class Config : public BaseConfig {
 private:
    uint64_t openblockthreshold;
    bool openblock;
    bool reqcount;
    bool opcount;
    bool recordchannel;
    bool recorddie;
    bool recordblock;
    bool recordpage;

 public:
  Config();

  bool setConfig(const char *, const char *) override;
  void update() override;

  uint64_t readUint(uint32_t) override;
  bool readBoolean(uint32_t) override;
};

}

}

#endif
