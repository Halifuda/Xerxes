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

#include "pal/config.hh"

#include "util/simplessd.hh"

namespace SimpleSSD {

namespace PAL {

const char NAME_CHANNEL[] = "Channel";
const char NAME_PACKAGE[] = "Package";
const char NAME_PAGE_ALLOCATION[] = "PageAllocation";
const char NAME_SUPER_BLOCK[] = "SuperblockSize";

/* NAND config TODO: seperate this */
const char NAME_DIE[] = "Die";
const char NAME_PLANE[] = "Plane";
const char NAME_BLOCK[] = "Block";
const char NAME_PAGE[] = "Page";
const char NAME_PAGE_SIZE[] = "PageSize";
const char NAME_SUBPAGE_NUM[] = "SubPageNum";
const char NAME_USE_MULTI_PLANE_OP[] = "EnableMultiPlaneOperation";
const char NAME_USE_AMPI[] = "EnableAMPI";
const char NAME_USE_SUSPEND[] = "EnablePESuspend";
const char NAME_USE_SCA[] = "EnableSCA";
const char NAME_DMA_SPEED[] = "DMASpeed";
const char NAME_DMA_WIDTH[] = "DMAWidth";
const char NAME_FLASH_TYPE[] = "NANDType";

/* NAND timing TODO: seperate this */
const char NAME_NAND_LSB_READ[] = "LSBRead";
const char NAME_NAND_LSB_WRITE[] = "LSBWrite";
const char NAME_NAND_CSB_READ[] = "CSBRead";
const char NAME_NAND_CSB_WRITE[] = "CSBWrite";
const char NAME_NAND_MSB_READ[] = "MSBRead";
const char NAME_NAND_MSB_WRITE[] = "MSBWrite";
const char NAME_NAND_ERASE[] = "Erase";

/* NAND power TODO: seperate this */
const char NAME_NAND_VOLTAGE[] = "Voltage";
const char NAME_NAND_CURRENT_READ[] = "ReadCurrent";
const char NAME_NAND_CURRENT_PROGRAM[] = "ProgramCurrent";
const char NAME_NAND_CURRENT_ERASE[] = "EraseCurrent";
const char NAME_NAND_CURRENT_IDLE[] = "IdleCurrent";
const char NAME_NAND_CURRENT_STANDBY[] = "StandbyCurrent";

// AYDNOTICE: new latency model
const char NAME_USE_NEW_LATENCY[] = "UseNewLatencyModel";

const char NAME_CMDADDR_BUS_WIDTH[] = "CmdAddrBusWidth";
const char NAME_CMDADDR_BUS_SPEED[] = "CmdAddrBusSpeed";
const char NAME_DATA_BUS_WIDTH[] = "DataBusWidth";
const char NAME_DATA_BUS_SPEED[] = "DataBusSpeed";
const char NAME_CAD_BUS_WIDTH[] = "CmdAddrDataBusWidth";
const char NAME_CAD_BUS_SPEED[] = "CmdAddrDataBusSpeed";
// AYDTODO: READ using old data
const char NAME_NAND_LSB_ISPP_PROGRAM[] = "LSBisppProgramPhase";
const char NAME_NAND_LSB_ISPP_VERIFY[] = "LSBisppVerifyPhase";
const char NAME_NAND_LSB_ISPP_ITER[] = "LSBisppIters";

const char NAME_NAND_CSB_ISPP_PROGRAM[] = "CSBisppProgramPhase";
const char NAME_NAND_CSB_ISPP_VERIFY[] = "CSBisppVerifyPhase";
const char NAME_NAND_CSB_ISPP_ITER[] = "CSBisppIters";

const char NAME_NAND_MSB_ISPP_PROGRAM[] = "MSBisppProgramPhase";
const char NAME_NAND_MSB_ISPP_VERIFY[] = "MSBisppVerifyPhase";
const char NAME_NAND_MSB_ISPP_ITER[] = "MSBisppIters";

const char NAME_NAND_ERASE_PHASE[] = "ErasePhase";
const char NAME_NAND_ERASE_VERIFY[] = "EraseVerifyPhase";
const char NAME_NAND_ERASE_VOLTAGE_RESET[] = "EraseVoltageReset";

const char NAME_NAND_LOAD_WRITE_BUFFER[] = "LoadWriteBuffer";
const char NAME_NAND_WRITE_MAX_SUSPEND[] = "pageMaxWriteSuspendTime";
const char NAME_NAND_ERASE_MAX_SUSPEND[] = "blockMaxEraseSuspendTime";

/* Constants for calculating DMA time based on ONFI 3.x spec */
// READ : <00h> <C1> <C2> <R1> <R2> <R3> <30h> [tWB] [tR] [tRR] <DATA>
// WRITE: <80h> <C1> <C2> <R1> <R2> <R3> [tADL] <DATA> <10h> [tWB] [tPROG]
// ERASE: <60h> <R1> <R2> <R3> <D0h> [tWB] [tBERS]
const uint8_t readCycle = 16;
const uint8_t writeCycle = 8;
const uint8_t eraseCycle = 6;

Config::Config() {
  channel = 8;
  package = 4;
  die = 2;
  plane = 1;
  block = 512;
  page = 512;
  pageSize = 16384;
  subpageNum = 1;
  useMultiPlaneOperation = true;
  useAMPI = false;
  useSuspend = false;
  useSCA = false;
  dmaSpeed = 400;
  dmaWidth = 8;
  nandType = NAND_MLC;

  nandTiming.useNewModel = false;

  nandTiming.new_lsb.read = 40000000;
  nandTiming.new_lsb.program = 20000000;
  nandTiming.new_lsb.verify = 8000000;
  nandTiming.new_lsb.isppiter = 5;

  // Default MLC
  nandTiming.new_csb.read = 0;
  nandTiming.new_csb.program = 0;
  nandTiming.new_csb.verify = 0;
  nandTiming.new_lsb.isppiter = 0;

  nandTiming.new_msb.read = 65000000;
  nandTiming.new_msb.program = 20000000;
  nandTiming.new_msb.verify = 24000000;
  nandTiming.new_lsb.isppiter = 15;

  nandTiming.new_erase.erase = 3000000000;
  nandTiming.new_erase.verify = 2000000000;
  nandTiming.new_erase.vreset = 4000000;

  nandTiming.isppconf.loadbuffer = 3000000;
  nandTiming.isppconf.max_write_suspend = 30;
  nandTiming.isppconf.max_erase_suspend = 10;

  nandTiming.cabus.width = 8;
  nandTiming.cabus.speed = 400;
  nandTiming.dbus.width = 8;
  nandTiming.dbus.speed = 400;
  nandTiming.cad_multiplex_bus.width = 8;
  nandTiming.cad_multiplex_bus.speed = 400;

  // Set NAND timing (Default: MLC, csb is not used)
  nandTiming.lsb.read = 40000000;    // 40us
  nandTiming.lsb.write = 500000000;  // 500us
  nandTiming.csb.read = 0;
  nandTiming.csb.write = 0;
  nandTiming.msb.read = 65000000;     // 65us
  nandTiming.msb.write = 1300000000;  // 1300us
  nandTiming.erase = 3500000000;      // 3.5ms

  // Set NAND power (From: Micron's MT29F64*)
  nandPower.voltage = 3300;           // 3.3V
  nandPower.current.read = 25000;     // 25mA
  nandPower.current.program = 25000;  // 25mA
  nandPower.current.erase = 25000;    // 25mA
  nandPower.current.busIdle = 5000;   // 5mA
  nandPower.current.standby = 10;     // 10uA

  superblock = 0;
  memset(PageAllocation, 0, 4);
}

bool Config::setConfig(const char *name, const char *value) {
  bool ret = true;

  if (MATCH_NAME(NAME_CHANNEL)) {
    channel = strtoul(value, nullptr, 10);
  }
  else if (MATCH_NAME(NAME_PACKAGE)) {
    package = strtoul(value, nullptr, 10);
  }
  else if (MATCH_NAME(NAME_DIE)) {
    die = strtoul(value, nullptr, 10);
  }
  else if (MATCH_NAME(NAME_PLANE)) {
    plane = strtoul(value, nullptr, 10);
  }
  else if (MATCH_NAME(NAME_BLOCK)) {
    block = strtoul(value, nullptr, 10);
  }
  else if (MATCH_NAME(NAME_PAGE)) {
    page = strtoul(value, nullptr, 10);
  }
  else if (MATCH_NAME(NAME_PAGE_SIZE)) {
    pageSize = strtoul(value, nullptr, 10);
  }
  else if (MATCH_NAME(NAME_SUBPAGE_NUM)) {
    subpageNum = strtoul(value, nullptr, 10);
  }
  else if (MATCH_NAME(NAME_USE_MULTI_PLANE_OP)) {
    useMultiPlaneOperation = convertBool(value);
  }
  else if (MATCH_NAME(NAME_USE_AMPI)) {
    useAMPI = convertBool(value);
  }
  else if (MATCH_NAME(NAME_USE_SUSPEND)) {
    useSuspend = convertBool(value);
  }
  else if (MATCH_NAME(NAME_USE_SCA)) {
    useSCA = convertBool(value);
  } 
  else if (MATCH_NAME(NAME_DMA_SPEED)) {
    dmaSpeed = strtoul(value, nullptr, 10);
  }
  else if (MATCH_NAME(NAME_DMA_WIDTH)) {
    dmaWidth = strtoul(value, nullptr, 10);
  }
  else if (MATCH_NAME(NAME_FLASH_TYPE)) {
    nandType = (NAND_TYPE)strtoul(value, nullptr, 10);
  }
  else if (MATCH_NAME(NAME_SUPER_BLOCK)) {
    _superblock = value;
  }
  else if (MATCH_NAME(NAME_PAGE_ALLOCATION)) {
    _pageAllocation = value;
  }
  else if (MATCH_NAME(NAME_NAND_LSB_READ)) {
    nandTiming.lsb.read = strtoul(value, nullptr, 10);
    // AYDTODO: whether to use old
    nandTiming.new_lsb.read = nandTiming.lsb.read;
  }
  else if (MATCH_NAME(NAME_NAND_LSB_WRITE)) {
    nandTiming.lsb.write = strtoul(value, nullptr, 10);
  }
  else if (MATCH_NAME(NAME_NAND_CSB_READ)) {
    nandTiming.csb.read = strtoul(value, nullptr, 10);
    // AYDTODO: whether to use old
    nandTiming.new_csb.read = nandTiming.csb.read;
  }
  else if (MATCH_NAME(NAME_NAND_CSB_WRITE)) {
    nandTiming.csb.write = strtoul(value, nullptr, 10);
  }
  else if (MATCH_NAME(NAME_NAND_MSB_READ)) {
    nandTiming.msb.read = strtoul(value, nullptr, 10);
    // AYDTODO: whether to use old
    nandTiming.new_msb.read = nandTiming.msb.read;
  }
  else if (MATCH_NAME(NAME_NAND_MSB_WRITE)) {
    nandTiming.msb.write = strtoul(value, nullptr, 10);
  }
  else if (MATCH_NAME(NAME_NAND_ERASE)) {
    nandTiming.erase = strtoul(value, nullptr, 10);
  }
  else if (MATCH_NAME(NAME_NAND_VOLTAGE)) {
    nandPower.voltage = strtoul(value, nullptr, 10);
  }
  else if (MATCH_NAME(NAME_NAND_CURRENT_READ)) {
    nandPower.current.read = strtoul(value, nullptr, 10);
  }
  else if (MATCH_NAME(NAME_NAND_CURRENT_PROGRAM)) {
    nandPower.current.program = strtoul(value, nullptr, 10);
  }
  else if (MATCH_NAME(NAME_NAND_CURRENT_ERASE)) {
    nandPower.current.erase = strtoul(value, nullptr, 10);
  }
  else if (MATCH_NAME(NAME_NAND_CURRENT_IDLE)) {
    nandPower.current.busIdle = strtoul(value, nullptr, 10);
  }
  else if (MATCH_NAME(NAME_NAND_CURRENT_STANDBY)) {
    nandPower.current.standby = strtoul(value, nullptr, 10);
  }
  // AYDNOTICE: use new latency
  else if (MATCH_NAME(NAME_USE_NEW_LATENCY)) {
    nandTiming.useNewModel = convertBool(value);
    // printf("Config use new model = %d\n", nandTiming.useNewModel);
  }
  // AYDNOTICE: new model buses
  else if (MATCH_NAME(NAME_CMDADDR_BUS_WIDTH)) {
    nandTiming.cabus.width = strtoul(value, nullptr, 10);
    // printf("Config cabus width = %lu\n", nandTiming.cabus.width);
  }
  else if (MATCH_NAME(NAME_CMDADDR_BUS_SPEED)) {
    nandTiming.cabus.speed = strtoul(value, nullptr, 10);
    // printf("Config cabus speed = %lu\n", nandTiming.cabus.speed);
  }
  else if (MATCH_NAME(NAME_DATA_BUS_WIDTH)) {
    nandTiming.dbus.width = strtoul(value, nullptr, 10);
    // printf("Config dbus width = %lu\n", nandTiming.dbus.width);
  }
  else if (MATCH_NAME(NAME_DATA_BUS_SPEED)) {
    nandTiming.dbus.speed = strtoul(value, nullptr, 10);
    // printf("Config dbus speed = %lu\n", nandTiming.dbus.speed);
  }
  else if (MATCH_NAME(NAME_CAD_BUS_WIDTH)) {
    nandTiming.cad_multiplex_bus.width = strtoul(value, nullptr, 10);
  }
  else if (MATCH_NAME(NAME_CAD_BUS_SPEED)) {
    nandTiming.cad_multiplex_bus.speed = strtoul(value, nullptr, 10);
  }
  // AYDNOTICE: new model ISPP Program
  else if (MATCH_NAME(NAME_NAND_LSB_ISPP_PROGRAM)) {
    nandTiming.new_lsb.program = strtoul(value, nullptr, 10);
    // printf("Config lsb program phase = %lu\n", nandTiming.new_lsb.program);
  }
  else if (MATCH_NAME(NAME_NAND_LSB_ISPP_VERIFY)) {
    nandTiming.new_lsb.verify = strtoul(value, nullptr, 10);
    // printf("Config lsb verify phase = %lu\n", nandTiming.new_lsb.verify);
  }
  else if (MATCH_NAME(NAME_NAND_LSB_ISPP_ITER)) {
    nandTiming.new_lsb.isppiter = strtoul(value, nullptr, 10);
    // printf("Config lsb ispp iter = %lu\n", nandTiming.new_lsb.isppiter);
  }
  else if (MATCH_NAME(NAME_NAND_CSB_ISPP_PROGRAM)) {
    nandTiming.new_csb.program = strtoul(value, nullptr, 10);
    // printf("Config csb program phase = %lu\n", nandTiming.new_csb.program);
  }
  else if (MATCH_NAME(NAME_NAND_CSB_ISPP_VERIFY)) {
    nandTiming.new_csb.verify = strtoul(value, nullptr, 10);
    // printf("Config csb verify phase = %lu\n", nandTiming.new_csb.verify);
  }
  else if (MATCH_NAME(NAME_NAND_CSB_ISPP_ITER)) {
    nandTiming.new_csb.isppiter = strtoul(value, nullptr, 10);
    // printf("Config csb ispp iter = %lu\n", nandTiming.new_csb.isppiter);
  }
  else if (MATCH_NAME(NAME_NAND_MSB_ISPP_PROGRAM)) {
    nandTiming.new_msb.program = strtoul(value, nullptr, 10);
    // printf("Config msb program phase = %lu\n", nandTiming.new_msb.program);
  }
  else if (MATCH_NAME(NAME_NAND_MSB_ISPP_VERIFY)) {
    nandTiming.new_msb.verify = strtoul(value, nullptr, 10);
    // printf("Config msb verify phase = %lu\n", nandTiming.new_msb.verify);
  }
  else if (MATCH_NAME(NAME_NAND_MSB_ISPP_ITER)) {
    nandTiming.new_msb.isppiter = strtoul(value, nullptr, 10);
    // printf("Config msb ispp iter = %lu\n", nandTiming.new_msb.isppiter);
  }
  // AYDNOTICE: new model erase
  else if (MATCH_NAME(NAME_NAND_ERASE_PHASE)) {
    nandTiming.new_erase.erase = strtoul(value, nullptr, 10);
    // printf("Config erase phase = %lu\n", nandTiming.new_erase.erase);
  }
  else if (MATCH_NAME(NAME_NAND_ERASE_VERIFY)) {
    nandTiming.new_erase.verify = strtoul(value, nullptr, 10);
    // printf("Config erase verify = %lu\n", nandTiming.new_erase.verify);
  }
  else if (MATCH_NAME(NAME_NAND_ERASE_VOLTAGE_RESET)) {
    nandTiming.new_erase.vreset = strtoul(value, nullptr, 10);
    // printf("Config erase voltage reset = %lu\n", nandTiming.new_erase.vreset);
  }
  // AYDNOTICE: new model ISPP config
  else if (MATCH_NAME(NAME_NAND_LOAD_WRITE_BUFFER)) {
    nandTiming.isppconf.loadbuffer = strtoul(value, nullptr, 10);
    // printf("Config data buffer load = %lu\n", nandTiming.isppconf.loadbuffer);
  }
  else if (MATCH_NAME(NAME_NAND_WRITE_MAX_SUSPEND)) {
    nandTiming.isppconf.max_write_suspend = strtoul(value, nullptr, 10);
    // printf("Config max page write suspend = %lu\n", 
    //         nandTiming.isppconf.max_write_suspend);
  }
  else if (MATCH_NAME(NAME_NAND_ERASE_MAX_SUSPEND)) {
    nandTiming.isppconf.max_erase_suspend = strtoul(value, nullptr, 10);
    // printf("Config nax block erase suspend = %lu\n",
    //         nandTiming.isppconf.max_erase_suspend);
  }
  else {
    ret = false;
  }

  return ret;
}

void Config::update() {
  if (dmaWidth & 0x07) {
    panic("dmaWidth should be multiple of 8.");
  }

  // DMA time calculation
  //                 MT/s       MT -> T    ms     us     ns     ps
  float tCK = 1.f / (dmaSpeed * 1048576) * 1000 * 1000 * 1000 * 1000;
  float tWC = 25.f * 1000;

  nandTiming.dma0.read = readCycle * tWC;
  nandTiming.dma0.write = (pageSize) * tCK / (dmaWidth / 8) + writeCycle * tWC;
  nandTiming.dma0.erase = eraseCycle * tWC;
  nandTiming.dma1.read = pageSize * tCK / (dmaWidth / 8) + 1 * tWC;
  nandTiming.dma1.write = 1 * tWC;
  nandTiming.dma1.erase = 1 * tWC;

  float caCK =
      1.f / (nandTiming.cabus.speed * 1048576) * 1000 * 1000 * 1000 * 1000;
  nandTiming.cabus.read = readCycle * caCK * 8 / nandTiming.cabus.width;
  nandTiming.cabus.write = writeCycle * caCK * 8 / nandTiming.cabus.width;
  nandTiming.cabus.erase = eraseCycle * caCK * 8 / nandTiming.cabus.width;
  nandTiming.cabus.dma1 = 1 * caCK * 8 / nandTiming.cabus.width;

  float dCK =
      1.f / (nandTiming.dbus.speed * 1048576) * 1000 * 1000 * 1000 * 1000;
  nandTiming.dbus.read = pageSize * dCK * 8 / nandTiming.dbus.width;
  nandTiming.dbus.write = pageSize * dCK * 8/ nandTiming.dbus.width;
  nandTiming.dbus.erase = 0; // Unused.
  nandTiming.dbus.dma1 = 0; // Unused.

  // Parse page allocation setting
  int i = 0;
  uint8_t check = 0;
  bool fail = false;

  for (auto iter : _pageAllocation) {
    if ((iter == 'C') | (iter == 'c')) {
      if (check & INDEX_CHANNEL) {
        fail = true;
      }

      PageAllocation[i++] = INDEX_CHANNEL;
      check |= INDEX_CHANNEL;
    }
    else if ((iter == 'W') | (iter == 'w')) {
      if (check & INDEX_PACKAGE) {
        fail = true;
      }

      PageAllocation[i++] = INDEX_PACKAGE;
      check |= INDEX_PACKAGE;
    }
    else if ((iter == 'D') | (iter == 'd')) {
      if (check & INDEX_DIE) {
        fail = true;
      }

      PageAllocation[i++] = INDEX_DIE;
      check |= INDEX_DIE;
    }
    else if ((iter == 'P') | (iter == 'p')) {
      if (check & INDEX_PLANE) {
        fail = true;
      }

      PageAllocation[i++] = INDEX_PLANE;
      check |= INDEX_PLANE;
    }

    if (i == 4) {
      break;
    }
  }

  if (check != (INDEX_CHANNEL | INDEX_PACKAGE | INDEX_DIE | INDEX_PLANE)) {
    fail = true;
  }

  if (useMultiPlaneOperation) {
    // Move P to front
    for (i = 0; i < 4; i++) {
      if (PageAllocation[i] == INDEX_PLANE) {
        for (int j = i; j > 0; j--) {
          PageAllocation[j] = PageAllocation[j - 1];
        }

        PageAllocation[0] = INDEX_PLANE;

        break;
      }
    }
  }

  if (fail) {
    panic("Invalid page allocation string");
  }

  // Parse super block size setting
  if (_superblock.length() > 0) {
    superblock = 0x00;

    for (auto iter : _superblock) {
      if ((iter == 'C') | (iter == 'c')) {
        superblock |= INDEX_CHANNEL;
      }
      else if ((iter == 'W') | (iter == 'w')) {
        superblock |= INDEX_PACKAGE;
      }
      else if ((iter == 'D') | (iter == 'd')) {
        superblock |= INDEX_DIE;
      }
      else if ((iter == 'P') | (iter == 'p')) {
        superblock |= INDEX_PLANE;
      }
    }
  }

  if (useMultiPlaneOperation) {
    superblock |= INDEX_PLANE;
  }
}

int64_t Config::readInt(uint32_t idx) {
  int64_t ret = 0;

  switch (idx) {
    case NAND_FLASH_TYPE:
      ret = nandType;
      break;
  }

  return ret;
}

uint64_t Config::readUint(uint32_t idx) {
  uint64_t ret = 0;

  switch (idx) {
    case PAL_CHANNEL:
      ret = channel;
      break;
    case PAL_PACKAGE:
      ret = package;
      break;
    case NAND_DIE:
      ret = die;
      break;
    case NAND_PLANE:
      ret = plane;
      break;
    case NAND_BLOCK:
      ret = block;
      break;
    case NAND_PAGE:
      ret = page;
      break;
    case NAND_PAGE_SIZE:
      ret = pageSize;
      break;
    case NAND_SUBPAGE_NUM:
      ret = subpageNum;
      break;
    case NAND_DMA_SPEED:
      ret = dmaSpeed;
      break;
    case NAND_DMA_WIDTH:
      ret = dmaWidth;
      break;
    case MAX_PAGE_WRITE_SUSPEND:
      ret = nandTiming.isppconf.max_write_suspend;
      break;
    case MAX_BLOCK_ERASE_SUSPEND:
      ret = nandTiming.isppconf.max_erase_suspend;
      break;
  }

  return ret;
}

bool Config::readBoolean(uint32_t idx) {
  bool ret = false;

  switch (idx) {
    case NAND_USE_MULTI_PLANE_OP:
      ret = useMultiPlaneOperation;
      break;
    case NAND_USE_AMPI:
      ret = useAMPI;
      break;
    case NAND_USE_SUSPEND:
      ret = useSuspend;
      break;
    case NAND_USE_SCA:
      ret = useSCA;
      break;
  }

  return ret;
}

uint8_t Config::getSuperblockConfig() {
  return superblock;
}

uint32_t Config::getPageAllocationConfig() {
  return (uint32_t)PageAllocation[0] | ((uint32_t)PageAllocation[1] << 8) |
         ((uint32_t)PageAllocation[2] << 16) |
         ((uint32_t)PageAllocation[3] << 24);
}

Config::NANDTiming *Config::getNANDTiming() {
  return &nandTiming;
}

Config::NANDPower *Config::getNANDPower() {
  return &nandPower;
}

}  // namespace PAL

}  // namespace SimpleSSD
