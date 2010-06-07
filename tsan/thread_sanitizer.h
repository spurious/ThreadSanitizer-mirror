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
//--------- Head ------------------- {{{1
#ifndef THREAD_SANITIZER_H_
#define THREAD_SANITIZER_H_

#include "ts_util.h"

//--------- Utils ------------------- {{{1
#include "ts_util.h"

void Report(const char *format, ...);
void PcToStrings(uintptr_t pc, bool demangle,
                string *img_name, string *rtn_name,
                string *file_name, int *line_no);
string PcToRtnNameAndFilePos(uintptr_t pc);
string PcToRtnName(uintptr_t pc, bool demangle);
string Demangle(const char *str);


//--------- FLAGS ---------------------------------- {{{1
struct FLAGS {
  bool             ignore_stack;
  intptr_t         verbosity;
  bool             show_stats;
  bool             show_expected_races;
  uintptr_t        trace_addr;
  uintptr_t        segment_set_recycle_queue_size;
  uintptr_t        recent_segments_cache_size;
  vector<string>   file_prefix_to_cut;
  vector<string>   ignore;
  vector<string>   cut_stack_below;
  string           summary_file;
  string           log_file;
  bool             offline;
  intptr_t         max_n_threads;
  bool             compress_cache_lines;
  bool             unlock_on_mutex_destroy;

  intptr_t         sample_events;
  intptr_t         sample_events_depth;

  intptr_t         num_callers;

  intptr_t    keep_history;
  bool        pure_happens_before;
  bool        free_is_write;
  bool        exit_after_main;
  bool        demangle;
  bool        announce_threads;
  bool        full_output;
  bool        show_states;
  bool        show_proc_self_status;
  bool        show_valgrind_context;  // debug-only
  bool        suggest_happens_before_arcs;
  bool        show_pc;
  bool        color;  // Colorify terminal output.
  bool        html;  // Output in html format.

  intptr_t  debug_level;
  vector<string> debug_phase;
  intptr_t  trace_level;

  intptr_t     dry_run;
  intptr_t     max_sid;
  intptr_t     max_mem_in_mb;
  intptr_t     num_callers_in_history;
  intptr_t     flush_period;

  intptr_t     literace_sampling;

  bool         separate_analysis_thread;

  bool         report_races;
  bool         thread_coverage;
  bool         call_coverage;
  string       dump_events;  // The name of log file. Debug mode only.
  bool         symbolize;
  bool         attach_mode;

  string       offline_syntax;

  string       tsan_program_name;
  string       tsan_url;

  vector<string> suppressions;
  bool           generate_suppressions;

  intptr_t     error_exitcode;
  bool         trace_children;

  vector<string> race_verifier;
  vector<string> race_verifier_extra;
  intptr_t       race_verifier_sleep_ms;
};

extern FLAGS *G_flags;

extern bool g_race_verifier_active;

extern bool debug_expected_races;
extern bool debug_malloc;
extern bool debug_free;
extern bool debug_thread;
extern bool debug_rtn;
extern bool debug_wrap;
extern bool debug_ins;
extern bool debug_shadow_stack;
extern bool debug_race_verifier;

//--------- TS Exports ----------------- {{{1
#include "ts_events.h"
#include "ts_trace_info.h"

extern void ThreadSanitizerInit();
extern void ThreadSanitizerFini();
extern void ThreadSanitizerHandleOneEvent(Event *event);
extern void ThreadSanitizerHandleMemoryAccess(int32_t tid, uintptr_t pc,
                                              uintptr_t addr, uintptr_t size,
                                              bool is_w);
extern void ThreadSanitizerHandleTrace(int32_t tid, TraceInfo *trace_info,
                                       uintptr_t *tleb);
extern void ThreadSanitizerHandleStackMemChange(int32_t tid, uintptr_t addr,
                                                uintptr_t size);
extern void ThreadSanitizerParseFlags(vector<string>* args);
extern bool ThreadSanitizerWantToInstrumentSblock(uintptr_t pc);
extern bool ThreadSanitizerWantToCreateSegmentsOnSblockEntry(uintptr_t pc);
extern bool ThreadSanitizerIgnoreAccessesBelowFunction(uintptr_t pc);

extern void ThreadSanitizerEnterSblock(int32_t tid, uintptr_t pc);

enum IGNORE_BELOW_RTN {
  IGNORE_BELOW_RTN_UNKNOWN,
  IGNORE_BELOW_RTN_NO,
  IGNORE_BELOW_RTN_YES
};

extern void ThreadSanitizerHandleRtnCall(int32_t tid, uintptr_t call_pc,
                                         uintptr_t target_pc,
                                         IGNORE_BELOW_RTN ignore_below);

extern void ThreadSanitizerHandleRtnExit(int32_t tid);
extern void ThreadSanitizerPrintUsage();
extern const char *ThreadSanitizerQuery(const char *query);
extern bool PhaseDebugIsOn(const char *phase_name);

extern bool g_has_entered_main;
extern bool g_has_exited_main;

// -------- Stats ------------------- {{{1
#include "ts_stats.h"
extern Stats *G_stats;

// -------- Expected Race ---------------------- {{{1
// Information about expected races.
struct ExpectedRace {
  uintptr_t   ptr;
  uintptr_t   size;
  bool        is_benign;
  bool        is_verifiable;
  int         count;
  const char *description;
  uintptr_t   pc;
};

ExpectedRace* ThreadSanitizerFindExpectedRace(uintptr_t addr);

// end. {{{1
#endif  //  THREAD_SANITIZER_H_

// vim:shiftwidth=2:softtabstop=2:expandtab
