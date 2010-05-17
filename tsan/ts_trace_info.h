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

// Information about one TRACE (single-entry-multiple-exit region of code).
// Author: Konstantin Serebryany.
#ifndef TS_TRACE_INFO_
#define TS_TRACE_INFO_

#include "ts_util.h"
// Information about one Memory Operation.
//
// Regular memory access is represented by mop[idx] = {pc,size,is_write}
// which is computed at instrumentation time and {actual_address} computed
// at run-time. The instrumentation insn looks like
//  tleb[idx] = actual_address
//
// There is a special case for some stores: *a = b
// Sometimes we may want to ignore stores that don't change the program state
// (i.e. (*a == b) before the store).
// So, we create two MopInfo objects:
//  mop[idx]   = {pc=0, size=0, is_write=true}
//  mop[idx+1] = {pc=pc_of_the_insn, size=size_of_store, is_write=true}
// and add two instrumentation insns:
//  tleb[idx]   = *a - b
//  tleb[idx+1] = a
//
//  This special case is used to avoid reporting benign races on vptr:
//  http://code.google.com/p/data-race-test/wiki/PopularDataRaces#Data_race_on_vptr
struct MopInfo {
  uintptr_t pc;
  uint8_t   size;  // 0, 1, 2, 4, 8, 10, or 16
  bool      is_write;
};

// An instance of this class is created for each TRACE (SEME region)
// during instrumentation.
class TraceInfo {
 public:
  static TraceInfo *NewTraceInfo(size_t n_mops, uintptr_t pc, bool dtor_head);
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
  bool   dtor_head() const { return dtor_head_; }

  static void PrintTraceProfile();

 private:
  TraceInfo() { }

  size_t n_mops_;
  size_t pc_;
  size_t id_;
  size_t counter_;
  bool   dtor_head_;
  MopInfo mops_[1];

  static size_t id_counter_;
  static vector<TraceInfo*> *g_all_traces;
};

// We declare static members here.  So, this file should not be included into
// mulitple .cc files within the same program.
size_t TraceInfo::id_counter_;
vector<TraceInfo*> *TraceInfo::g_all_traces;

TraceInfo *TraceInfo::NewTraceInfo(size_t n_mops, uintptr_t pc,
                                   bool dtor_head) {
  size_t mem_size = (sizeof(TraceInfo) + (n_mops - 1) * sizeof(MopInfo));
  uint8_t *mem = new uint8_t[mem_size];
  memset(mem, 0xab, mem_size);
  TraceInfo *res = new (mem) TraceInfo;
  res->n_mops_ = n_mops;
  res->pc_ = pc;
  res->id_ = id_counter_++;
  res->counter_ = 0;
  res->dtor_head_ = dtor_head;
  if (g_all_traces == NULL) {
    g_all_traces = new vector<TraceInfo*>;
    CHECK(id_counter_ == 1);
  }
  g_all_traces->push_back(res);
  return res;
}

void TraceInfo::PrintTraceProfile() {
  int64_t total_counter = 0;
  multimap<size_t, TraceInfo*> traces;
  for (size_t i = 0; i < g_all_traces->size(); i++) {
    TraceInfo *trace = (*g_all_traces)[i];
    traces.insert(make_pair(trace->counter(), trace));
    total_counter += trace->counter();
  }
  Printf("TraceProfile: %ld traces, %lld hits\n",
         g_all_traces->size(), total_counter);
  int i = 0;
  for (multimap<size_t, TraceInfo*>::reverse_iterator it = traces.rbegin();
       it != traces.rend(); ++it, i++) {
    TraceInfo *trace = it->second;
    int64_t c = it->first;
    int64_t permile = (c * 1000) / total_counter;
    if (permile == 0 || i >= 20) break;
    Printf("TR%ld c=%lld (%lld/1000) n_mops=%ld\n", trace->id(), c,
           permile, trace->n_mops());
  }
}

// end. {{{1
#endif  // TS_TRACE_INFO_
// vim:shiftwidth=2:softtabstop=2:expandtab:tw=80
