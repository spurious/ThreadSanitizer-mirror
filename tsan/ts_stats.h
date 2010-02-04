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
#ifndef TS_STATS_
#define TS_STATS_

#include "ts_util.h"

// Statistic counters for the entire tool.
struct Stats {
  Stats() {
    memset(this, 0, sizeof(*this));
  }

  void PrintStats() {
    PrintEventStats();
    Printf("   VTS: created small/big: %'ld / %'ld; "
           "deleted: %'ld; cloned: %'ld\n",
           vts_create_small, vts_create_big, vts_delete, vts_clone);
    Printf("   vts_total_size  = %'ld; avg=%'ld\n",
           vts_total_size,
           vts_total_size / (vts_create_small + vts_create_big + 1));
    Printf("   n_seg_hb        = %'ld\n", n_seg_hb);
    Printf("   n_vts_hb        = %'ld\n", n_vts_hb);
    Printf("   n_vts_hb_cached = %'ld\n", n_vts_hb_cached);
    Printf("   memory access:\n"
           "     1: %'ld\n"
           "     2: %'ld\n"
           "     4: %'ld\n"
           "     8: %'ld\n"
           "     s: %'ld\n",
           n_access1, n_access2, n_access4, n_access8, n_access_slow);
    PrintStatsForCache();
//    Printf("   Mops:\n"
//           "    total  = %'ld\n"
//           "    unique = %'ld\n",
//           mops_total, mops_uniq);
    Printf("   Publish: set: %'ld; get: %'ld; clear: %'ld\n",
           publish_set, publish_get, publish_clear);

    Printf("   PcTo: all: %'ld rtn: %'ld\n", pc_to_strings, pc_to_rtn_name);

    Printf("   StackTrace: create: %'ld; delete %'ld\n",
           stack_trace_create, stack_trace_delete);

    Printf("   History segments: same: %'ld; reuse: %'ld; new: %'ld\n",
           history_uses_same_segment, history_reuses_segment,
           history_creates_new_segment);
    Printf("   Forget all history: %'ld\n", n_forgets);

    if (G_flags->fast_mode)
      Printf("   Fast mode: first time      : %'ld;\n"
             "              still in creator: %'ld;\n"
             "              transition      : %'ld;\n"
             "              mt              : %'ld;\n",
             fast_mode_first_time, fast_mode_still_in_creator,
             fast_mode_transition, fast_mode_mt);
    PrintStatsForSeg();
    PrintStatsForSS();
    PrintStatsForLS();
  }

  void PrintStatsForSS() {
    Printf("   SegmentSet: created: %'ld; reused: %'ld;"
           " find: %'ld; recycle: %'ld\n",
           ss_create, ss_reuse, ss_find, ss_recycle);
    Printf("        sizes: 2: %'ld; 3: %'ld; 4: %'ld; other: %'ld\n",
           ss_size_2, ss_size_3, ss_size_4, ss_size_other);

    // SSEq is called at least (ss_find + ss_recycle) times since
    // FindExistingOrAlocateAndCopy calls map_.find()
    // and RecycleOneSegmentSet calls map_.erase(it)
    // Both find() and erase(it) require at least one call to SSHash and SSEq.
    //
    // Apart from SSHash call locations mentioned above,
    // SSHash is called for each AllocateAndCopy (ss_create + ss_reuse) times
    // for insert() AFTER it has already been called
    // by FindExistingOrAlocateAndCopy in case find() returned map_.end().
    // Hence the factor of 2.
    uintptr_t sseq_estimated = ss_find + ss_recycle,
            sshash_estimated = sseq_estimated + 2 * (ss_create + ss_reuse);
    Printf("   SSHash called %12ld times (vs. %12ld = +%d%%)\n"
           "   SSEq   called %12ld times (vs. %12ld = +%d%%)\n",
            sshash_calls, sshash_estimated,
            (sshash_calls - sshash_estimated)/(sshash_estimated/100 + 1),
            sseq_calls,   sseq_estimated,
            (sseq_calls   - sseq_estimated  )/(sseq_estimated/100 + 1));
  }
  void PrintStatsForCache() {
    Printf("   Cache:\n"
           "    fast      = %'ld\n"
           "    new       = %'ld\n"
           "    delete    = %'ld\n"
           "    fetch     = %'ld\n"
           "    storage   = %'ld\n",
           cache_fast_get, cache_new_line,
           cache_delete_empty_line, cache_fetch,
           cache_max_storage_size);
  }

  void PrintStatsForSeg() {
    Printf("   Segment: created: %'ld; reused: %'ld\n",
           seg_create, seg_reuse);
  }

  void PrintStatsForLS() {
    Printf("   LockSet add: 0: %'ld; 1 : %'ld; n : %'ld\n",
           ls_add_to_empty, ls_add_to_singleton, ls_add_to_multi);
    Printf("   LockSet rem: 1: %'ld; n : %'ld\n",
           ls_remove_from_singleton, ls_remove_from_multi);
    Printf("   LockSet cache: add : %'ld; rem : %'ld; fast: %'ld\n",
           ls_add_cache_hit, ls_rem_cache_hit, ls_cache_fast);
  }

  void PrintEventStats() {
    uintptr_t total = 0;
    for (int i = 0; i < LAST_EVENT; i++) {
      if (events[i]) {
        Printf("  %25s: %'ld\n", Event::TypeString((EventType)i),
               events[i]);
      }
      total += events[i];
    }
    Printf("  %25s: %'ld\n", "Total", total);
    const int n_sizes = sizeof(memory_access_sizes_) /
                        sizeof(memory_access_sizes_[0]);
    for (int i = 0; i < n_sizes; i++) {
      if (memory_access_sizes_[i]) {
        Printf("  mop[%d]: %'ld\n", i, memory_access_sizes_[i]);
      }
    }
    for (size_t i = 0;
         i < sizeof(mops_per_trace) / sizeof(mops_per_trace[0]); i++) {
      Printf("  mop_per_trace[%d] = %'ld\n", i, mops_per_trace[i]);
    }
  }


  uintptr_t memory_access_sizes_[18];
  uintptr_t events[LAST_EVENT];

  uintptr_t n_vts_hb;
  uintptr_t n_vts_hb_cached;
  uintptr_t n_seg_hb;

  uintptr_t ls_add_to_empty, ls_add_to_singleton, ls_add_to_multi,
            ls_remove_from_singleton, ls_remove_from_multi,
            ls_add_cache_hit, ls_rem_cache_hit,
            ls_cache_fast;

  uintptr_t n_access1, n_access2, n_access4, n_access8, n_access_slow;

  uintptr_t cache_fast_get;
  uintptr_t cache_new_line;
  uintptr_t cache_delete_empty_line;
  uintptr_t cache_fetch;
  uintptr_t cache_max_storage_size;

  uintptr_t mops_total;
  uintptr_t mops_uniq;
  uintptr_t mops_per_trace[16];

  uintptr_t vts_create_big, vts_create_small,
            vts_clone, vts_delete, vts_total_size;

  uintptr_t ss_create, ss_reuse, ss_find, ss_recycle;
  uintptr_t ss_size_2, ss_size_3, ss_size_4, ss_size_other;

  uintptr_t sshash_calls, sseq_calls;

  uintptr_t seg_create, seg_reuse;

  uintptr_t publish_set, publish_get, publish_clear;

  uintptr_t fast_mode_first_time, fast_mode_still_in_creator,
            fast_mode_transition, fast_mode_mt;

  uintptr_t pc_to_strings, pc_to_rtn_name;

  uintptr_t stack_trace_create, stack_trace_delete;

  uintptr_t history_uses_same_segment, history_creates_new_segment,
            history_reuses_segment;

  uintptr_t n_forgets;
};


// end. {{{1
#endif  // TS_STATS_
// vim:shiftwidth=2:softtabstop=2:expandtab:tw=80
