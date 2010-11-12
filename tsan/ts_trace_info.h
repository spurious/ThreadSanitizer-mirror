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
// Information about one TRACE (single-entry-multiple-exit region of code).
#ifndef TS_TRACE_INFO_
#define TS_TRACE_INFO_

#include "ts_util.h"
// Information about one Memory Operation.
//
// A memory access is represented by mop[idx] = {pc,size,is_write}
// which is computed at instrumentation time and {actual_address} computed
// at run-time. The instrumentation insn looks like
//  tleb[idx] = actual_address
struct MopInfo {
  uintptr_t pc;
  uint8_t   size;  // 0, 1, 2, 4, 8, 10, or 16
  bool      is_write;
};


class TraceInfoPOD {
 public:
  size_t n_mops_;
  size_t pc_;
  size_t id_;
  size_t counter_;
  bool   generate_segments_;
  MopInfo mops_[1];
};

// An instance of this class is created for each TRACE (SEME region)
// during instrumentation.
class TraceInfo : protected TraceInfoPOD {
 public:
  static TraceInfo *NewTraceInfo(size_t n_mops, uintptr_t pc);
  void DeleteTraceInfo(TraceInfo *trace_info) {
    delete [] (uintptr_t*)trace_info;
  }
  MopInfo *GetMop(size_t i) {
    DCHECK(i < n_mops_);
    return &mops_[i];
  }

  size_t n_mops() const { return n_mops_; }
  size_t pc()     const { return pc_; }
  size_t id()     const { return id_; }
  size_t &counter()     { return counter_; }
  bool   generate_segments() const { return generate_segments_; }

  static void PrintTraceProfile();

 private:
  static size_t id_counter_;
  static vector<TraceInfo*> *g_all_traces;

  TraceInfo() : TraceInfoPOD() { }
};



// end. {{{1
#endif  // TS_TRACE_INFO_
// vim:shiftwidth=2:softtabstop=2:expandtab:tw=80
