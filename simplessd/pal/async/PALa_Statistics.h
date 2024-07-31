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
 * Authors:
 */
#ifndef __PALa_Statistics_h__
#define __PALa_Statistics_h__

#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "sim/statistics.hh"
#include "util/old/SimpleSSD_types.h"

struct OperStat {
  uint64_t cmdaddrwait;
  uint64_t cmdaddr;
  uint64_t datawait;
  uint64_t data;
  uint64_t flashwait;
  uint64_t flash;
  uint64_t count;
  double energy;

  OperStat()
      : cmdaddrwait(0),
        cmdaddr(0),
        datawait(0),
        data(0),
        flashwait(0),
        flash(0),
        count(0),
        energy(0.) {}

  void reset() {
    cmdaddrwait = 0;
    cmdaddr = 0;
    datawait = 0;
    data = 0;
    flashwait = 0;
    flash = 0;
    count = 0;
    energy = 0.;
  }
};

struct AddrHash {
  const static size_t sch = 13;
  const static size_t spk = 53;
  const static size_t sdi = 97;
  const static size_t spl = 115;
  const static size_t sbl = 295;
  const static size_t spg = 385;
  size_t operator()(const CPDPBP &addr) const {
    return addr.Channel * sch + addr.Package * spk + addr.Die * sdi +
           addr.Plane * spl + addr.Block * sbl + addr.Page * spg;
  }
};

struct AddrEqual {
  bool operator()(const CPDPBP &a, const CPDPBP &b) const {
    return a.Channel == b.Channel && a.Package == b.Package && a.Die == b.Die &&
           a.Plane == b.Plane && a.Block == b.Block && a.Page == b.Page;
  }
};

typedef std::unordered_map<CPDPBP, uint64_t, AddrHash, AddrEqual> SuspendStat;

class ChannelStatistics {
  uint64_t last_reset_tick;

  uint64_t cabusy, dbusy, dietotalbusy;

  OperStat read, write, erase;

  SuspendStat write_suspend;
  SuspendStat erase_suspend;

 public:
  uint64_t numDies;
  
  ChannelStatistics()
      : last_reset_tick(0), cabusy(0), dbusy(0), dietotalbusy(0), numDies(0) {}

  OperStat *getOperStat(PAL_OPERATION oper) {
    if (oper == OPER_READ) {
      return &read;
    }
    else if (oper == OPER_WRITE) {
      return &write;
    }
    return &erase;
  }

  uint64_t *getCABusyCounter() { return &cabusy; }
  uint64_t *getDataBusyCounter() { return &dbusy; }
  uint64_t *getDieBusyCounter() { return &dietotalbusy; }

  void updateSuspend(PAL_OPERATION oper, CPDPBP addr);
  uint64_t getSuspend(PAL_OPERATION oper, CPDPBP addr);

  void getStatList(std::vector<SimpleSSD::Stats> &list, std::string prefix);
  void getStatValues(std::vector<double> &values);

  void reset();
};

#endif