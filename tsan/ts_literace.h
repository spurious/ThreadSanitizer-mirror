/*
  This file is part of ThreadSanitizer, a dynamic data race detector.

  Copyright (C) 2008-2010 Google Inc
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

#ifndef TS_LITERACE_H_
#define TS_LITERACE_H_

#include "ts_util.h"

// ---------------- Lite Race ------------------ {{{1
// Experimental!
//
// The idea was first introduced in LiteRace:
// http://www.cs.ucla.edu/~dlmarino/pubs/pldi09.pdf
// Instead of analyzing all memory accesses, we do sampling.
// For each trace (single-enry muliple-exit region) we maintain a counter of
// executions. If a trace has been executed more than a certain threshold, we
// start skipping this trace sometimes.
// The LiteRace paper suggests several strategies for sampling, including
// thread-local counters. Having thread local counters for all threads is too
// expensive, so we have 8 arrays of counters and use the array (tid % 8).
//
// TODO(kcc): this currently does not work with --keep-history=0
//
// Note: ANNOTATE_PUBLISH_MEMORY() does not work with sampling... :(


static const size_t n_literace_counters = 1024 * 1024;
static const size_t n_literace_threads = 8;
static uint32_t literace_counters[n_literace_threads][n_literace_counters];


#ifdef _MSC_VER
#include <intrin.h>
inline unsigned u32_log2(unsigned x) {
  unsigned long y;
  _BitScanReverse(&y, x);
  return y;
}
#endif

#ifdef __GNUC__
inline unsigned u32_log2(unsigned x) {
  return 31 - __builtin_clz(x);
}
#endif

INLINE bool LiteRaceSkipTrace(int tid, uint32_t trace_no, uint32_t sampling_rate) {
  DCHECK(u32_log2(1) == 0);
  DCHECK(u32_log2(1 << 4) == 4);
  DCHECK(u32_log2(1 << 30) == 30);

  if (sampling_rate == 0) return false;

  // sampling_rate indicates the level of sampling.
  // 0 means no sampling.
  // 1 means handle *almost* all accesses.
  // ...
  // 31 means very aggressive sampling (skip a lot of accesses).

  trace_no %= n_literace_counters;

  uint32_t counter = ++literace_counters[tid % n_literace_threads][trace_no];
  CHECK(sampling_rate < 32);
  int shift = 32 - sampling_rate;
  DCHECK(shift >= 1);
  int high_bits = counter >> shift;
  if (high_bits) {  // counter is big enough.
    int n_high_bits = u32_log2(high_bits);
    uint32_t mask = (1U << n_high_bits) - 1;
    // The higher the value of the counter, the bigger the probability that we
    // will skip this trace.
    if ((counter & mask) != 0) {
      return true;
    }
  }
  return false;
}

// end. {{{1
#endif  // TS_LITERACE_H_
// vim:shiftwidth=2:softtabstop=2:expandtab:tw=80
