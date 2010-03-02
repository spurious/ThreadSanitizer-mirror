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
#ifndef TS_SIMPLE_CACHE_
#define TS_SIMPLE_CACHE_

#include "ts_util.h"

// Few simple 'cache' classes.
// -------- PtrToBoolCache ------ {{{1
template <int kSize>
class PtrToBoolCache {
 public:
  PtrToBoolCache() {
    Flush();
  }
  void Flush() {
    memset(this, 0, sizeof(*this));
  }
  void Insert(uintptr_t ptr, bool val) {
    size_t idx  = ptr % kSize;
    arr_[idx] = ptr;
    if (val) {
      bits_[idx / 32] |= 1U << (idx % 32);
    } else {
      bits_[idx / 32] &= ~(1U << (idx % 32));
    }
  }
  bool Lookup(uintptr_t ptr, bool *val) {
    size_t idx  = ptr % kSize;
    if (arr_[idx] == ptr) {
      *val = (bits_[idx / 32] >> (idx % 32)) & 1;
      return true;
    }
    return false;
  }
 private:
  uint64_t arr_[kSize];
  uint32_t bits_[(kSize + 31) / 32];
};

// -------- IntPairToBoolCache ------ {{{1
template <int kSize>
class IntPairToBoolCache {
 public:
  IntPairToBoolCache() {
    Flush();
  }
  void Flush() {
    memset(arr_, 0, sizeof(arr_));
  }
  void Insert(int a, int b, bool val) {
    uint64_t comb = combine2(a, b);
    uint64_t idx  = comb % kSize;
    if (val) {
      comb |= 1ULL << 63;
    }
    arr_[idx] = comb;
  }
  bool Lookup(int a, int b, bool *val) {
    uint64_t comb = combine2(a, b);
    uint64_t idx  = comb % kSize;
    uint64_t prev = arr_[idx];
    uint64_t valbit = prev & (1ULL << 63);
    if ((prev & ~(1ULL << 63)) == comb) {
      *val = valbit != 0;
      return true;
    }
    return false;
  }
 private:
  uint64_t combine2(int a, int b) {
    CHECK_GT(a, 0);
    CHECK_GT(b, 0);
    int64_t x = a;
    return (x << 32) | b;
  }
  uint64_t arr_[kSize];
};

// end. {{{1
#endif  // TS_SIMPLE_CACHE_
// vim:shiftwidth=2:softtabstop=2:expandtab:tw=80
