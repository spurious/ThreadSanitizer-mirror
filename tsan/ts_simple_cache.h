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
    CHECK((kSize % sizeof(uintptr_t)) == 0);
    Flush();
  }
  void Flush() {
    memset(this, 0, sizeof(*this));
  }
  void Insert(uintptr_t ptr, bool val) {
    size_t idx  = ptr % 32;
    arr_[idx] = ptr;
    if (val) {
      bits_[idx / 32] |= 1U << (idx % 32);
    } else {
      bits_[idx / 32] &= ~(1U << (idx % 32));
    }
  }
  bool Lookup(uintptr_t ptr, bool *val) {
    size_t idx  = ptr % 32;
    if (arr_[idx] == ptr) {
      return bits_[idx / 32] >> (idx % 32);
    }
    return false;
  }
 private:
  uint64_t arr_[kSize];
  uint32_t bits_[kSize / 32];
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

// -------- PairCache & IntPairToIntCache ------ {{{1
template <typename A, typename B, typename Ret,
         int kHtableSize, int kArraySize = 8>
class PairCache {
 public:
  PairCache() {
    CHECK(sizeof(Entry) == sizeof(A) + sizeof(B) + sizeof(Ret));
    Flush();
  }
  void Flush() {
    memset(this, 0, sizeof(*this));
  }
  void Insert(A a, B b, Ret v) {
    // fill the hash table
    if (kHtableSize != 0) {
      uint32_t idx  = compute_idx(a, b);
      htable_[idx].Fill(a, b, v);
    }

    // fill the array
    Ret dummy;
    if (kArraySize != 0 && !ArrayLookup(a, b, &dummy)) {
      int pos = array_pos_;
      array_[pos].Fill(a, b, v);
      array_pos_ = (array_pos_ + 1) % (kArraySize);
    }
  }
  bool Lookup(A a, B b, Ret *v) {
    // check the array
    if (kArraySize != 0 && ArrayLookup(a, b, v)) {
      G_stats->ls_cache_fast++;
      return true;
    }
    // check the hash table.
    if (kHtableSize != 0) {
      uint32_t idx  = compute_idx(a, b);
      Entry & prev_e = htable_[idx];
      if (prev_e.Match(a, b)) {
        *v = prev_e.v;
        return true;
      }
    }
    return false;
  }
 private:
  struct Entry {
    A a;
    B b;
    Ret v;
    void Fill(A a, B b, Ret v) {
      this->a = a;
      this->b = b;
      this->v = v;
    }
    bool Match(A a, B b) const {
      return this->a == a && this->b == b;
    }
  };

  bool ArrayLookup(A a, B b, Ret *v) {
    for (int i = 0; i < kArraySize; i++) {
      Entry & entry = array_[i];
      if (entry.Match(a, b)) {
        *v = entry.v;
        return true;
      }
    }
    return false;
  }

  uint32_t compute_idx(A a, B b) {
    if (kHtableSize == 0)
      return 0;
    else
      return (combine2(a, b) % max(kHtableSize, 1));
  }

  static uint32_t combine2(int a, int b) {
    return (a << 16) ^ b;
  }

  static uint32_t combine2(SSID a, SID b) {
    return combine2(a.raw(), b.raw());
  }

  Entry htable_[kHtableSize];

  Entry array_[kArraySize];
  int array_pos_;
};

template<int kHtableSize, int kArraySize = 8>
class IntPairToIntCache
  : public PairCache<int, int, int, kHtableSize, kArraySize> {};


// end. {{{1
#endif  // TS_SIMPLE_CACHE_
// vim:shiftwidth=2:softtabstop=2:expandtab:tw=80
