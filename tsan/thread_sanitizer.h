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
#ifndef __THREAD_SANITIZER_H__
#define __THREAD_SANITIZER_H__

#include <glob.h>
#include <stdint.h>

#include "ts_setup.h"

#undef NDEBUG  // Assert is always on.

// Valgrind compilation is the default one.
// Define TS_NO_VALGRIND if you compile w/o valgrind.
#ifndef TS_NO_VALGRIND
# include "ts_valgrind.h"
# define TS_VALGRIND
# define CHECK tl_assert
#else
# include <assert.h>
# define CHECK assert
#endif // TS_NO_VALGRIND


#ifdef TS_USE_STANDARD_STL
#include <string.h>
#include <limits.h>
#include <set>
#include <map>
#include <hash_map>
#include <vector>
#include <deque>
#include <stack>
#include <algorithm>
#include <string>
#include <bitset>
//#include <algorithm>
#include "ext/algorithm"
#include "ext/hash_map"
#include "ext/hash_set"
using __gnu_cxx::hash_map;
using __gnu_cxx::hash_set;
using __gnu_cxx::lexicographical_compare_3way;

#else  // no TS_USE_STANDARD_STL
#include "stlport/set"
#include "stlport/map"
#include "stlport/hash_map"
#include "stlport/hash_set"
#include "stlport/vector"
#include "stlport/deque"
#include "stlport/stack"
#include "stlport/algorithm"
#include "stlport/string"
#include "stlport/bitset"
#include "stlport/algorithm"

using std::hash_map;
using std::hash_set;
using std::lexicographical_compare_3way;
#endif  // TS_USE_STANDARD_STL


using std::set;
using std::multiset;
using std::multimap;
using std::map;
using std::deque;
using std::stack;
using std::string;
using std::vector;
using std::bitset;

using std::min;
using std::max;
using std::sort;
using std::pair;
using std::make_pair;
using std::unique_copy;


#ifdef TS_VALGRIND

// TODO(kcc) get rid of these macros.
#define sprintf(arg1, arg2...) VG_(sprintf)((Char*)arg1, (HChar*)arg2)
#define vsnprintf(a1, a2, a3, a4) VG_(vsnprintf)((Char*)a1, a2, a3, a4)
#define getpid VG_(getpid)
#define strchr(a,b)    VG_(strchr)((Char*)a,b)
#define strdup(a) (char*)VG_(strdup)((HChar*)"strdup", (const Char*)a)
#define snprintf(a,b,c...)     VG_(snprintf)((Char*)a,b,c)
#define exit VG_(exit)
#define read VG_(read)
#define getenv(x) VG_(getenv)((Char*)x)
#define close VG_(close)
#define write VG_(write)
#define abort VG_(abort)
#define usleep(a) /*nothing. TODO.*/



static inline ThreadId GetVgTid() {
  extern ThreadId VG_(running_tid); // HACK: avoid calling get_running_tid()
  ThreadId res = VG_(running_tid);
  //DCHECK(res == VG_(get_running_tid)());
  return res;
}

#else // No TS_VALGRIND
#include <unistd.h>


#define UNIMPLEMENTED() CHECK(0 == 42)

#define UNLIKELY(x) __builtin_expect((x), 0)
#define LIKELY(x)   __builtin_expect(!!(x), 1)

class ScopedMallocCostCenter {
 public:
  ScopedMallocCostCenter(const char *cc) {
    // UNIMPLEMENTED();
  }
};


extern uintptr_t GetPcOfCurrentThread();


#endif // TS_VALGRIND


template <class T, const char **cc>
class CCAlloc : public std::allocator<T> {
 public:
  T* allocate(long n, const void *hint = 0) {
    ScopedMallocCostCenter cost_center(*cc);
    return std::allocator<T>::allocate(n, hint);
  }
};





#define CHECK_GT(X, Y) CHECK((X) >  (Y))
#define CHECK_LT(X, Y) CHECK((X) < (Y))
#define CHECK_GE(X, Y) CHECK((X) >= (Y))
#define CHECK_LE(X, Y) CHECK((X) <= (Y))
#define CHECK_NE(X, Y) CHECK((X) != (Y))
#define CHECK_EQ(X, Y) CHECK((X) == (Y))



#if defined(DEBUG) && DEBUG >= 1
  #define DCHECK(a) CHECK(a)
  #define DEBUG_MODE (1)
  #define INLINE
#else
  #define DCHECK(a) do { if (0) { if (a) {} } } while(0)
  #define DEBUG_MODE (0)
  #define INLINE  inline  __attribute__ ((always_inline))
#endif

#define NOINLINE __attribute__ ((noinline))



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
  vector<string>   file_prefix_to_cut;
  vector<string>   ignore;
  vector<string>   cut_stack_below;
  string           summary_file;
  intptr_t         max_n_threads;
  bool             compress_cache_lines;
  bool             unlock_on_mutex_destroy;

  intptr_t         sample_events;
  intptr_t         sample_events_depth;

  intptr_t         num_callers;

  intptr_t    keep_history;
  bool        fast_mode;
  bool        pure_happens_before;
  bool        ignore_in_dtor;
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
  intptr_t  trace_level;

  intptr_t     dry_run;
  intptr_t     max_sid;
  intptr_t     max_mem_in_mb;
  intptr_t     num_callers_in_history;
  bool         report_races;
  bool         thread_coverage;
  bool         dump_events;
  bool         symbolize;
};

extern FLAGS *G_flags;

//--------- TS Exports ----------------- {{{1

enum EventType {
  NOOP,
  READ,
  WRITE,
  LOCK_BEFORE,
  READER_LOCK,
  WRITER_LOCK,
  UNLOCK,
  UNLOCK_OR_INIT,
  LOCK_CREATE,
  LOCK_DESTROY,
  BUS_LOCK_ACQUIRE,
  BUS_LOCK_RELEASE,
  THR_SET_PTID,
  THR_START,
  THR_FIRST_INSN,
  THR_END,
  THR_JOIN_BEFORE,
  THR_JOIN_AFTER,
  RTN_EXIT,
  RTN_CALL,
  SBLOCK_ENTER,
  BBLOCK_ENTER,
  SIGNAL,
  WAIT_BEFORE,
  WAIT_AFTER,
  TWAIT_AFTER,
  BARRIER_BEFORE,
  BARRIER_AFTER,
  PCQ_CREATE,
  PCQ_DESTROY,
  PCQ_PUT,
  PCQ_GET,
  SP_CHANGE,
  STACK_MEM_NEW,
  STACK_MEM_DIE,
  MALLOC,
  FREE,
  PUBLISH_RANGE,
  UNPUBLISH_RANGE,
  HB_LOCK,
  IGNORE_READS_BEG,
  IGNORE_READS_END,
  IGNORE_WRITES_BEG,
  IGNORE_WRITES_END,
  SET_THREAD_NAME,
  SET_LOCK_NAME,
  TRACE_MEM,
  EXPECT_RACE,
  VERBOSITY,
  STACK_TRACE,
  LAST_EVENT
};


static const char *kEventNames[] = {
  "NOOP",
  "READ",
  "WRITE",
  "LOCK_BEFORE",
  "READER_LOCK",
  "WRITER_LOCK",
  "UNLOCK",
  "UNLOCK_OR_INIT",
  "LOCK_CREATE",
  "LOCK_DESTROY",
  "BUS_LOCK_ACQUIRE",
  "BUS_LOCK_RELEASE",
  "THR_SET_PTID",
  "THR_START",
  "THR_FIRST_INSN",
  "THR_END",
  "THR_JOIN_BEFORE",
  "THR_JOIN_AFTER",
  "RTN_EXIT",
  "RTN_CALL",
  "SBLOCK_ENTER",
  "BBLOCK_ENTER",
  "SIGNAL",
  "WAIT_BEFORE",
  "WAIT_AFTER",
  "TWAIT_AFTER",
  "BARRIER_BEFORE",
  "BARRIER_AFTER",
  "PCQ_CREATE",
  "PCQ_DESTROY",
  "PCQ_PUT",
  "PCQ_GET",
  "SP_CHANGE",
  "STACK_MEM_NEW",
  "STACK_MEM_DIE",
  "MALLOC",
  "FREE",
  "PUBLISH_RANGE",
  "UNPUBLISH_RANGE",
  "HB_LOCK",
  "IGNORE_READS_BEG",
  "IGNORE_READS_END",
  "IGNORE_WRITES_BEG",
  "IGNORE_WRITES_END",
  "SET_THREAD_NAME",
  "SET_LOCK_NAME",
  "TRACE_MEM",
  "EXPECT_RACE",
  "VERBOSITY",
  "STACK_TRACE",
  "LAST_EVENT"
};


class Event;

struct ThreadSanitizerReport {
  // Types of reports.
  enum ReportType {
    DATA_RACE,
    UNLOCK_FOREIGN,
    UNLOCK_NONLOCKED,
    INVALID_LOCK
  };

  ReportType type;

  virtual ~ThreadSanitizerReport() { }
};

extern void ThreadSanitizerInit();
extern void ThreadSanitizerFini();
extern void ThreadSanitizerHandleOneEvent(Event *event);
extern void ThreadSanitizerHandleMemoryAccess(int32_t tid,
                                              uintptr_t addr, uintptr_t size,
                                              bool is_w);
extern void ThreadSanitizerHandleStackMemChange(int32_t tid, uintptr_t addr,
                                                uintptr_t size, bool is_new);
extern void ThreadSanitizerParseFlags(vector<string>* args);
extern bool ThreadSanitizerWantToInstrumentSblock(uintptr_t pc);

extern void ThreadSanitizerEnterSblock(int32_t tid, uintptr_t pc);
extern void ThreadSanitizerHandleRtnCall(int32_t tid, uintptr_t call_pc,
                                         uintptr_t target_pc);
extern void ThreadSanitizerHandleRtnExit(int32_t tid);
extern void ThreadSanitizerPrintReport(ThreadSanitizerReport *report);
extern void ThreadSanitizerPrintUsage();

extern bool g_so_far_only_one_thread;
extern bool g_has_entered_main;
extern bool g_has_exited_main;

//--------- Event ------------------------- {{{1
class Event {
 public:
  Event(EventType type, int32_t tid, uintptr_t pc, uintptr_t a, uintptr_t info)
      : type_(type),
      tid_(tid),
      pc_(pc),
      a_(a),
      info_(info) {
      }
  Event() {}  // Not initialized.

  void Init(EventType type, int32_t tid, uintptr_t pc, uintptr_t a, uintptr_t info) {
    type_ = type;
    tid_  = tid;
    pc_   = pc;
    a_    = a;
    info_ = info;
  }


  EventType type()  const { return type_; }
  int32_t   tid()   const { return tid_; }
  uintptr_t a()     const { return a_; }
  uintptr_t pc()    const { return pc_; } 
  uintptr_t info()  const { return info_; } 
  void      Print() const { 
    Printf("T%d: %s [pc=%p; a=%p; i=%p]\n", 
           tid(), TypeString(type()), pc(), a(), info());

  }
  static const char *TypeString(EventType type) {
    return kEventNames[type];
  }
 private:
  EventType      type_;
  int32_t   tid_;
  uintptr_t pc_;
  uintptr_t a_;
  uintptr_t info_;
};

// end. {{{1
#endif  //  __THREAD_SANITIZER_H__
// vim:shiftwidth=2:softtabstop=2:expandtab
