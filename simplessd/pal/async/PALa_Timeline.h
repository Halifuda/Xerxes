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

#ifndef __PALa_Timeline_h__
#define __PALa_Timeline_h__

#include <set>

#include "sim/simulator.hh"
#include "util/old/SimpleSSD_types.h"

/// @brief Unit in TimeLine scheduling. 
/// Named 'a'TimeSlot for naming conflict avoidance.
class aTimeSlot {
 public:
  uint64_t start, end;
  /** Used for busyslot, checking Multi-Plane or AMPI. */
  CPDPBP busyAddr;
  /** Used for reschedule. */
  SimpleSSD::Event event;

  aTimeSlot() : start(1), end(0) {}
  aTimeSlot(uint64_t s, uint64_t e) : start(s), end(e) {}

  /// @brief Compare between two timeslots by their end ticks.
  bool operator<(aTimeSlot s) const { return end < s.end; }

  bool operator==(aTimeSlot s) const { return start == s.start && end == s.end; }

  /// @brief Cast a timeslot into bool, checking if it is valid.
  /// Valid: start <= end; Invalid: start > end.
  operator bool() const { return start <= end; }

  uint64_t len() const { return end - start; }

  /// @brief Check if this slot contain target slot.
  /// <true> if start <= s.start <= s.end <= end.
  bool contain(aTimeSlot s) const {
    return bool(*this) && bool(s) && (start <= s.start) && (end >= s.end);
  }

  /// @brief Split this slot into two at the given tick. Denote
  /// this as [s,e], then there will be [s,t] and [t,e]. The [s,t]
  /// will be returned, and this will be transformed into [t,e] if
  /// and only if <t> is contained in [s,e] (s <= t <= e).
  /// @param t Split tick.
  /// @return [s,t] if <t> is contained in [s,e], otherwise [1,0],
  /// which will be casted into <false> by bool().
  aTimeSlot split(uint64_t t);

  /// @brief Merge a slot with this one. This one will be
  /// transformed if and only if the two slots are adjacent.
  /// @return <true> if the merge is succeeded.
  bool merge(aTimeSlot s);
};

/// @brief Util of TimeLine scheduling, w/ some useful methods.
class TimeLine {
  std::set<aTimeSlot> freeSet;
  std::set<aTimeSlot> busySet;

  /// @brief Find a free slot to serve an interval. The slot is late 
  /// than the given interval, and is longer than the given interval.
  /// @param bound The bound of the given interval. (start >= bound)
  /// @param latency The length of the given interval.
  /// @return An iterator to <freeSet>. <end()> if no such slot.
  std::set<aTimeSlot>::iterator findFreeSlot(uint64_t bound, uint64_t latency);

  /// @brief Find a busy slot which is the same to given one.
  /// @param start The start tick of the given slot.
  /// @param end The end tick of the given slot.
  /// @return An iterator to <busySet>. <end()> if no such slot.
  std::set<aTimeSlot>::iterator findBusySlot(uint64_t start, uint64_t end);

 public:
  TimeLine() { freeSet.insert(aTimeSlot{0, INT64_MAX}); }
  ~TimeLine() { freeSet.clear(), busySet.clear(); }

  /// @brief Remove the slots that are useless in freeSet and busySet.
  /// @param tick Current tick. All the slots that ended before this 
  /// will be removed. 
  /// @param counter The counter to record the accumulated busy time.
  void flushTimeslot(uint64_t tick, uint64_t *counter);

  /// @brief The interface to call findFreeSlot. The found free slot 
  /// will be splited into a busy slot, a remained free slot, and 
  /// (may) a new free slot. [free] -> ([new-free])[busy][remain]. 
  /// The busy slot can serve the given interval.
  /// @param bound The bound of the given interval. (start >= bound)
  /// @param latency The length of the given interval.
  /// @return If the interval can be served, return a copy of the 
  /// busy slot. Otherwise return invalid TimeSLot. Invalid results 
  /// should be solved by caller.
  /// @see findFreeSlot()
  aTimeSlot allocateTimeSlot(uint64_t bound, uint64_t latency);

  /// @brief Get a busy slot which is the latest finishing one.
  /// @return A copy of the slot. If there is no busy slot, return an 
  /// invalid one ([1,0]).
  aTimeSlot getLatestBusySlot();

  /// @brief The interface to call findBusySlot. Find a slot in busy 
  /// set, which has the same start and end tick with the given one. 
  /// Then, delete it from busy set, and merge it into free set.
  /// @return <true> if there is such slot in busy set. 
  /// @warning Any <false> results should panic.
  /// @see findBusySlot()
  bool removeBusySlot(uint64_t start, uint64_t end);
};

#endif  // PALa_Timeline_h__