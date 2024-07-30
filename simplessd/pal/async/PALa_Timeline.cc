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

#include "PALa_Timeline.h"

#include "util/algorithm.hh"

aTimeSlot aTimeSlot::split(uint64_t t) {
  aTimeSlot S;
  if (start <= t && t <= end) {
    S = aTimeSlot(start, t);
    start = t;
  }
  return S;
}

bool aTimeSlot::merge(aTimeSlot s) {
  if ((s.end >= start && s.end <= end) || (end >= s.start && end <= s.end)) {
    uint64_t newstart = MIN(start, s.start);
    uint64_t newend = MAX(end, s.end);
    start = newstart;
    end = newend;
    return true;
  }
  return false;
}

std::set<aTimeSlot>::iterator TimeLine::findFreeSlot(uint64_t bound,
                                                     uint64_t latency) {
  aTimeSlot S(bound, bound + latency);
  auto iter = freeSet.lower_bound(S);
  while (iter != freeSet.end() && iter->len() < latency) iter++;
  return iter;
}

std::set<aTimeSlot>::iterator TimeLine::findBusySlot(uint64_t start,
                                                     uint64_t end) {
  aTimeSlot S(start, end);
  return busySet.find(S);
}

void TimeLine::flushTimeslot(uint64_t tick, uint64_t *counter) {
  for (auto iter = freeSet.begin(), itend = freeSet.end(); iter != itend;) {
    if (iter->end <= tick) {
      iter = freeSet.erase(iter);
    } else {
      ++iter;
    }
  }
  for (auto iter = busySet.begin(), itend = busySet.end(); iter != itend;) {
    if (iter->end <= tick) {
      // AYDNOTICE: For timeline scheduling will not 
      // overlap with itself, and we donot merge multiple
      // timeline to output statistics (now), so simply
      // add is correct.
      if (counter != nullptr) (*counter) += iter->len();
      iter = busySet.erase(iter);
    } else {
      ++iter;
    }
  }
}

aTimeSlot TimeLine::allocateTimeSlot(uint64_t bound, uint64_t latency) {
  auto iter = findFreeSlot(bound, latency);
  if (iter == freeSet.end()) {
    return aTimeSlot();
  }
  aTimeSlot free = *iter;
  freeSet.erase(iter);

  uint64_t oldstart = free.start;
  uint64_t newstart = MAX(free.start, bound);
  aTimeSlot busy = free.split(newstart + latency);
  if (oldstart < bound) {
    aTimeSlot newfree = busy.split(bound);
    freeSet.insert(newfree);
  }
  freeSet.insert(free);
  busySet.insert(aTimeSlot(busy));
  return busy;
}

aTimeSlot TimeLine::getLatestBusySlot() {
  auto iter = busySet.rbegin();
  if (iter != busySet.rend()) {
    return aTimeSlot(*iter);
  } else {
    return aTimeSlot();
  }
}

bool TimeLine::removeBusySlot(uint64_t start, uint64_t end) {
  auto busyIter = findBusySlot(start, end);
  if (busyIter == busySet.end()) {
    return false;
  }
  aTimeSlot busy = *busyIter;
  busySet.erase(busyIter);
  auto freeIter = freeSet.lower_bound(busy);
  aTimeSlot free = *freeIter;
  freeSet.erase(freeIter);
  if (!free.merge(busy)) {
    freeSet.insert(busy);
  }
  freeSet.insert(free);
  return true;
}