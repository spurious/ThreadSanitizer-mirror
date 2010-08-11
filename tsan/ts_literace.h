/* Copyright (c) 2008-2010, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// This file is part of ThreadSanitizer, a dynamic data race detector.
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
