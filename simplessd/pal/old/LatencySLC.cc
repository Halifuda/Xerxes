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
 */

#include "LatencySLC.h"

LatencySLC::LatencySLC(SimpleSSD::PAL::Config::NANDTiming t,
                       SimpleSSD::PAL::Config::NANDPower p)
    : Latency(t, p) {}

LatencySLC::~LatencySLC() {}

inline uint8_t LatencySLC::GetPageType(uint32_t) {
  return PAGE_LSB;
}

uint64_t LatencySLC::NewModelLatency(uint32_t, uint8_t op, uint8_t type) {
  switch (type) {
    case NEWLAT_CMDADDR:
      if(op == OPER_READ) {
        return timing.cabus.read;
      }
      else if (op == OPER_WRITE) {
        return timing.cabus.write;
      }
      else {
        return timing.cabus.erase;
      }
    case NEWLAT_DATAIN:
      if (op == OPER_WRITE) {
        return timing.dbus.write;
      }
      else {
        return 0;
      }
    case NEWLAT_DATAOUT:
      if (op == OPER_READ) {
        return timing.dbus.read;
      }
      else {
        return 0;
      }
    case NEWLAT_DMA1:
      return timing.cabus.dma1;
    case NEWLAT_ERASE_PHASE:
      return timing.new_erase.erase;
    case NEWLAT_ERASE_VERIFY:
      return timing.new_erase.verify;
    case NEWLAT_ERASE_VRESET:
      return timing.new_erase.vreset;
    case NEWLAT_LOAD_BUFFER:
      return timing.isppconf.loadbuffer;
    case NEWLAT_READ:
      return timing.new_lsb.read;
    case NEWLAT_ISPP_PROGRAM:
      return timing.new_lsb.program;
    case NEWLAT_ISPP_VERIFY:
      return timing.new_lsb.verify;
    case NEWLAT_ISPP_ITER:
      return timing.new_lsb.isppiter;
    default:
      break;
  }
  return 10;
}

uint64_t LatencySLC::GetLatency(uint32_t, uint8_t Oper, uint8_t Busy) {
  switch (Busy) {
    case BUSY_DMA0:
      if (Oper == OPER_READ) {
        return timing.dma0.read;
      }
      else if (Oper == OPER_WRITE) {
        return timing.dma0.write;
      }
      else {
        return timing.dma0.erase;
      }

      break;
    case BUSY_DMA1:
      if (Oper == OPER_READ) {
        return timing.dma1.read;
      }
      else if (Oper == OPER_WRITE) {
        return timing.dma1.write;
      }
      else {
        return timing.dma1.erase;
      }

      break;
    case BUSY_MEM:
      if (Oper == OPER_READ) {
        return timing.lsb.read;
      }
      else if (Oper == OPER_WRITE) {
        return timing.lsb.write;
      }
      else {
        return timing.erase;
      }

      break;
    default:
      break;
  }

  return 10;
}
