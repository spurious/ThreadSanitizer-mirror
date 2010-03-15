/*
  This file is part of ThreadSanitizer, a dynamic data race detector.

  Copyright (C) 2008-2009 Google Inc
     opensource@google.com

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of the
  License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
  02111-1307, USA.

  The GNU General Public License is contained in the file COPYING.
*/

// Author: Konstantin Serebryany.
// Author: Timur Iskhodzhanov.
#ifndef TS_HEAP_INFO_
#define TS_HEAP_INFO_

#include "ts_util.h"

// Information about heap memory.
// For each heap allocation we create a struct HeapInfo.
// This struct should have fields 'uintptr_t ptr' and 'uintptr_t size',
// a default CTOR and a copy CTOR.

template<class HeapInfo>
class HeapMap {
 public:
  typedef map<uintptr_t, HeapInfo> map_t;
  typedef typename map_t::iterator iterator;

  HeapMap() {
    // Insert a maximal and minimal possible values to make IsHeapMem simpler.
    HeapInfo max_info;
    max_info.ptr = (uintptr_t)-1;
    max_info.size = 0;
    map_[max_info.ptr] = max_info;

    HeapInfo min_info;
    min_info.ptr = 0;
    min_info.size = 0;
    map_[min_info.ptr] = min_info;
  }

  iterator begin() { return map_.begin(); }
  iterator end() { return map_.end(); }

  size_t size() { return map_.size() - 2; }

  void InsertInfo(uintptr_t a, HeapInfo info) {
    CHECK(IsValidPtr(a));
    CHECK(info.ptr == a);
    map_[a] = info;
  }

  bool GetInfo(uintptr_t a, HeapInfo *res) {
    CHECK(IsValidPtr(a));
    typename map_t::iterator it = map_.find(a);
    if (it == map_.end()) {
      return false;
    }
    *res = it->second;
    return true;
  }

  void EraseInfo(uintptr_t a) {
    CHECK(IsValidPtr(a));
    map_.erase(a);
  }

  bool IsHeapMem(uintptr_t a, HeapInfo *res) {
    CHECK(this);
    CHECK(IsValidPtr(a));
    typename map_t::iterator it = map_.lower_bound(a);
    CHECK(it != map_.end());
    if (it->second.ptr == a) {
      // Exact match. 'a' is the beginning of a heap-allocated address.
      *res = it->second;
      return true;
    }
    CHECK(a < it->second.ptr);
    CHECK(it != map_.begin());
    // not an exact match, try the previous iterator.
    --it;
    HeapInfo &info = it->second;
    CHECK(info.ptr < a);
    if (info.ptr + info.size > a) {
      // within the range.
      *res = info;
      return true;
    }
    return false;
  }

  void Clear() { map_.clear(); }

 private:
  bool IsValidPtr(uintptr_t a) {
    return a != 0 && a != (uintptr_t) -1;
  }
  map_t map_;
};

#endif  // TS_HEAP_INFO_
