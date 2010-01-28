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
//
// ******* WARNING ********
// This code is experimental. Do not expect anything here to work.
// ***** END WARNING ******

#include "pin.H"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <assert.h>

#include "ts_util.h"

#if defined(__GNUC__)
# include <cxxabi.h>  // __cxa_demangle
# define YIELD() usleep(0)
# define CAS(ptr,oldval,newval) __sync_bool_compare_and_swap(ptr,oldval,newval)
# define ATOMIC_READ(a) __sync_add_and_fetch(a, 0)

#elif defined(_MSC_VER)
namespace WINDOWS
{
// This is the way of including winows.h recommended by PIN docs.
#include<Windows.h>
}

# define YIELD() // __yield()
// TODO(kcc): how to demangle on windows?
// TODO(kcc): add actuall implementations
# define popen(x,y) (NULL)
# define CAS(ptr,oldval,newval) _InterlockedCompareExchange(ptr, newval, oldval)
# define ATOMIC_READ(a)         _InterlockedCompareExchange(a, 0, 0)
#endif

#include "thread_sanitizer.h"

#ifdef NDEBUG
# error "Please don't define NDEBUG"
#endif

//--------- Simple Lock ------------------ {{{1
class TSLock {
 public:
  TSLock() {
    InitLock(&lock_);
  }
  void Lock() {
    GetLock(&lock_, __LINE__);
  }
  void Unlock() {
    ReleaseLock(&lock_);
  }

 private:
  PIN_LOCK lock_;
};

class ScopedLock {
 public:
  ScopedLock(TSLock *lock)
    : lock_(lock) {
    lock_->Lock();
  }
  ~ScopedLock() { lock_->Unlock(); }
 private:
  TSLock *lock_;
};

//------ Global PIN lock ------- {{{1
class ScopedReentrantClientLock {
 public:
  ScopedReentrantClientLock(int line)
    : line_(line) {
    // if (line && G_flags->debug_level >= 5)  Printf("??Try  at line %d\n", line);
    PIN_LockClient();
    if (line && G_flags->debug_level >= 5)  Printf("++Lock at line %d\n", line);
  }
  ~ScopedReentrantClientLock() {
    if (line_ && G_flags->debug_level >= 5) Printf("--Unlock at line %d\n", line_);
    PIN_UnlockClient();
  }
 private:
  int line_;
};

//--------------- Aux classes ------------- {{{1
struct Routine {
  string rtn_name;
};

//--------------- Globals ----------------- {{{1
extern FILE *G_out;


// Maps address of rtn entry/ret to Routine*
static map<uintptr_t, Routine*> *routine_address_map;

static int64_t dyn_read_count;
static int64_t dyn_write_count;

static bool main_entered, main_exited;

// Number of threads created by pthread_create (i.e. not counting main thread).
static int n_created_threads = 0;

const uint32_t kMaxThreads = 100000;

static TSLock g_main_ts_lock;
static TSLock g_thread_create_lock;
// Under g_thread_create_lock.
static THREADID g_tid_of_thread_which_called_create_thread = -1;

static uintptr_t g_current_pc;

static bool g_attached_to_running_process = false;

//--------------- StackFrame ----------------- {{{1
struct StackFrame {
  uintptr_t pc;
  uintptr_t sp;
  StackFrame(uintptr_t p, uintptr_t s) : pc(p), sp(s) { }
};
//--------------- PinThread ----------------- {{{1
struct PinThread {
  volatile long last_child_tid;
  THREADID     parent_tid;
  size_t       last_malloc_size;
  size_t       last_mmap_size;
  pthread_t    my_ptid;
  bool         started;
  uintptr_t    cxa_guard;
  int          in_cxa_guard;
  vector<StackFrame> shadow_stack;
};

// Array of pin threads, indexed by pin's THREADID.
static PinThread *g_pin_threads;

#ifdef _MSC_VER
static hash_set<pthread_t> *g_win_handles_which_are_threads;
#endif

//------------- ThreadSanitizer exports ------------ {{{1
string Demangle(const char *str) {
#if defined(__GNUC__)
  int status;
  char *demangled = __cxxabiv1::__cxa_demangle(str, 0, 0, &status);
  if (demangled) {
    string res = demangled;
    free(demangled);
    return res;
  }
#endif
  return str;
}

void PcToStrings(uintptr_t pc, bool demangle,
                string *img_name, string *rtn_name,
                string *file_name, int *line_no) {
  if (G_flags->symbolize) {
    RTN rtn;
    ScopedReentrantClientLock lock(__LINE__);
    // ClientLock must be held.
    PIN_GetSourceLocation(pc, NULL, line_no, file_name);
    rtn = RTN_FindByAddress(pc);
    string name;
    if (RTN_Valid(rtn)) {
      *rtn_name = demangle
          ? Demangle(RTN_Name(rtn).c_str())
          : RTN_Name(rtn);
      *img_name = IMG_Name(SEC_Img(RTN_Sec(rtn)));
    }
  }
}

string PcToRtnName(uintptr_t pc, bool demangle) {
  string res;
  if (G_flags->symbolize) {
    RTN rtn;
    {
      ScopedReentrantClientLock lock(__LINE__);
      rtn = RTN_FindByAddress(pc);
    }
    if (RTN_Valid(rtn)) {
      res = demangle
          ? Demangle(RTN_Name(rtn).c_str())
          : RTN_Name(rtn);
    }
  }
  return res;
}

uintptr_t GetPcOfCurrentThread() {
  return g_current_pc;
}

//--------- DumpEvent ----------- {{{1
static void DumpEventPlainText(EventType type, int32_t tid, uintptr_t pc,
                               uintptr_t a, uintptr_t info) {
  static hash_set<uintptr_t> *pc_set;
  if (pc_set == NULL) {
    pc_set = new hash_set<uintptr_t>;
  }
  static FILE *log_file = NULL;
  if (log_file == NULL) {
    if (G_flags->log_file.empty()) {
      log_file = G_out;
    } else {
      log_file = popen(("gzip > " + G_flags->log_file).c_str(), "w");
    }
  }
  if (G_flags->symbolize && pc_set->insert(pc).second) {
    string img_name, rtn_name, file_name;
    int line = 0;
    PcToStrings(pc, false, &img_name, &rtn_name, &file_name, &line);
    if (file_name.empty()) file_name = "unknown";
    if (img_name.empty()) img_name = "unknown";
    if (rtn_name.empty()) rtn_name = "unknown";
    if (line == 0) line = 1;
    fprintf(log_file, "#PC %lx %s %s %s %d\n",
            (long)pc, img_name.c_str(), rtn_name.c_str(),
            file_name.c_str(), line);
  }
  fprintf(log_file, "%s %x %lx %lx %lx\n", kEventNames[type], tid,
          (long)pc, (long)a, (long)info);
}


// We have to send THR_START/THR_FIRST_INSN from here
// because we can't do it from CallbackForThreadStart() due to PIN's deadlock.
// TODO(kcc): Fix this!
static void DumpEventInternal(EventType type, int32_t tid, uintptr_t pc,
                              uintptr_t a, uintptr_t info) {
  Event event(type, tid, pc, a, info);

  if (DEBUG_MODE && G_flags->dump_events) {
    DumpEventPlainText(type, tid, pc, a, info);
    return;
  }
  if (DEBUG_MODE && G_flags->verbosity >= 3) {
    event.Print();
  }
  ThreadSanitizerHandleOneEvent(&event);
}

static void DumpEvent(EventType type, int32_t tid, uintptr_t pc,
                      uintptr_t a, uintptr_t info) {
  ScopedLock lock(&g_main_ts_lock);
  g_current_pc = pc;
  if (g_pin_threads[tid].started == false) {
    g_pin_threads[tid].started = true;
    DumpEventInternal(THR_START, tid, 0, 0, g_pin_threads[tid].parent_tid);
    DumpEventInternal(THR_FIRST_INSN, tid, 0, 0, 0);
  }
  DumpEventInternal(type, tid, pc, a, info);
}

//--------- Wraping and relacing --------------- {{{1
static bool RtnMatchesName(const string &rtn_name, const string &name) {
  CHECK(name.size() > 0);
  size_t pos = rtn_name.find(name);
  if (pos == string::npos) {
    return false;
  }
  if (pos == 0 && name.size() == rtn_name.size()) {
  //  Printf("Full match: %s %s\n", rtn_name.c_str(), name.c_str());
    return true;
  }
  if (pos == 0 && name.size() < rtn_name.size()
      && rtn_name[name.size()] == '@') {
  //  Printf("Versioned match: %s %s\n", rtn_name.c_str(), name.c_str());
    return true;
  }
  return false;
}

#define WRAP_PARAM4  THREADID tid, ADDRINT pc, CONTEXT *ctx, \
                                AFUNPTR f,\
                                uintptr_t arg0, uintptr_t arg1, \
                                uintptr_t arg2, uintptr_t arg3

#define WRAP_PARAM6 WRAP_PARAM4, uintptr_t arg4, uintptr_t arg5

static uintptr_t CallFun4(CONTEXT *ctx, THREADID tid,
                         AFUNPTR f, uintptr_t arg0, uintptr_t arg1,
                         uintptr_t arg2, uintptr_t arg3) {
  uintptr_t ret = 0xdeadbee1;
  PIN_CallApplicationFunction(ctx, tid,
                              CALLINGSTD_DEFAULT, (AFUNPTR)(f),
                              PIN_PARG(uintptr_t), &ret,
                              PIN_PARG(uintptr_t), arg0,
                              PIN_PARG(uintptr_t), arg1,
                              PIN_PARG(uintptr_t), arg2,
                              PIN_PARG(uintptr_t), arg3,
                              PIN_PARG_END());
  return ret;
}

#define CALL_ME_INSIDE_WRAPPER_4() CallFun4(ctx, tid, f, arg0, arg1, arg2, arg3)

// Completely replace (i.e. not wrap) a function with 3 (or less) parameters.
// The original function will not be called.
void ReplaceFunc3(IMG img, RTN rtn, const char *name, AFUNPTR replacement_func) {
  if (RTN_Valid(rtn) && RtnMatchesName(RTN_Name(rtn), name)) {
    Printf("RTN_ReplaceSignature on %s (%s)\n", name, IMG_Name(img).c_str());
    PROTO proto = PROTO_Allocate(PIN_PARG(uintptr_t),
                                 CALLINGSTD_DEFAULT,
                                 "proto",
                                 PIN_PARG(uintptr_t),
                                 PIN_PARG(uintptr_t),
                                 PIN_PARG(uintptr_t),
                                 PIN_PARG_END());
    RTN_ReplaceSignature(rtn,
                         AFUNPTR(replacement_func),
                         IARG_PROTOTYPE, proto,
                         IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                         IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
                         IARG_FUNCARG_ENTRYPOINT_VALUE, 2,
                         IARG_END);
    PROTO_Free(proto);
  }
}

// Wrap a function with up to 4 parameters.
void WrapFunc4(IMG img, RTN rtn, const char *name, AFUNPTR replacement_func) {
  if (RTN_Valid(rtn) && RtnMatchesName(RTN_Name(rtn), name)) {
    Printf("RTN_ReplaceSignature on %s (%s)\n", name, IMG_Name(img).c_str());
    PROTO proto = PROTO_Allocate(PIN_PARG(uintptr_t),
                                 CALLINGSTD_DEFAULT,
                                 "proto",
                                 PIN_PARG(uintptr_t),
                                 PIN_PARG(uintptr_t),
                                 PIN_PARG(uintptr_t),
                                 PIN_PARG(uintptr_t),
                                 PIN_PARG_END());
    RTN_ReplaceSignature(rtn,
                         AFUNPTR(replacement_func),
                         IARG_PROTOTYPE, proto,
                         IARG_THREAD_ID,
                         IARG_INST_PTR,
                         IARG_CONTEXT,
                         IARG_ORIG_FUNCPTR,
                         IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                         IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
                         IARG_FUNCARG_ENTRYPOINT_VALUE, 2,
                         IARG_FUNCARG_ENTRYPOINT_VALUE, 3,
                         IARG_END);
    PROTO_Free(proto);
  }
}

//--------- Instrumentation callbacks --------------- {{{1
//---------- Debug -----------------------------------{{{2
#define DEB_PR (0)

static void ShowPcAndSp(const char *where, THREADID tid,
                        ADDRINT pc, ADDRINT sp) {
    Printf("%s T%d sp=%ld %s\n", where, tid, sp,
           PcToRtnName(pc, true).c_str());
}

static void PrintShadowStack(THREADID tid) {
  PinThread &t = g_pin_threads[tid];
  Printf("T%d Shadow stack (%d)\n", tid, (int)t.shadow_stack.size());
  for (int i = t.shadow_stack.size() - 1; i >= 0; i--) {
    uintptr_t pc = t.shadow_stack[i].pc;
    uintptr_t sp = t.shadow_stack[i].sp;
    Printf("  sp=%ld pc=%lx %s\n", sp, pc, PcToRtnName(pc, true).c_str());
  }
}

static void DebugOnlyShowPcAndSp(const char *where, THREADID tid,
                                 ADDRINT pc, ADDRINT sp) {
  if (DEB_PR) {
    ShowPcAndSp(where, tid, pc, sp);
  }
}

static uintptr_t Wrap_ThreadSanitizerQuery(WRAP_PARAM4) {
  const char *query = (const char*)arg0;
  return (uintptr_t)ThreadSanitizerQuery(query);
}

//--------- Ignores -------------------------------- {{{2
static void IgnoreAllBegin(THREADID tid, ADDRINT pc) {
//  if (tid == 0) Printf("Ignore++ %d\n", z++);
  DumpEvent(IGNORE_READS_BEG, tid, pc, 0, 0);
  DumpEvent(IGNORE_WRITES_BEG, tid, pc, 0, 0);
}
static void IgnoreAllEnd(THREADID tid, ADDRINT pc) {
//  if (tid == 0) Printf("Ignore-- %d\n", z--);
  DumpEvent(IGNORE_READS_END, tid, pc, 0, 0);
  DumpEvent(IGNORE_WRITES_END, tid, pc, 0, 0);
}

//--------- __cxa_guard_* -------------------------- {{{2
// From gcc/cp/decl.c:
// --------------------------------------------------------------
//      Emit code to perform this initialization but once.  This code
//      looks like:
//
//      static <type> guard;
//      if (!guard.first_byte) {
//        if (__cxa_guard_acquire (&guard)) {
//          bool flag = false;
//          try {
//            // Do initialization.
//            flag = true; __cxa_guard_release (&guard);
//            // Register variable for destruction at end of program.
//           } catch {
//          if (!flag) __cxa_guard_abort (&guard);
//         }
//      }
// --------------------------------------------------------------
// So, when __cxa_guard_acquire returns true, we start ignoring all accesses
// and in __cxa_guard_release we stop ignoring them.
// We also need to ignore all accesses inside these two functions.

static void Before_cxa_guard_acquire(THREADID tid, ADDRINT pc, ADDRINT guard) {
//  PinThread &t = g_pin_threads[tid];
//  Printf("T%d A+ %lx\n", tid, guard);
//  t.cxa_guard = guard;
  IgnoreAllBegin(tid, pc);
}

static void After_cxa_guard_acquire(THREADID tid, ADDRINT pc, ADDRINT ret) {
  PinThread &t = g_pin_threads[tid];
  // Printf("T%d A- %lx ret=%d\n", tid, t.cxa_guard, (int)ret);
  if (ret) {
    // Continue ignoring, it will end in __cxa_guard_release.
    t.in_cxa_guard++;
  } else {
    // Stop ignoring, there will be no matching call to __cxa_guard_release.
    IgnoreAllEnd(tid, pc);
  }
}

static void Before_cxa_guard_release(THREADID tid, ADDRINT pc, ADDRINT guard) {
  PinThread &t = g_pin_threads[tid];
//  Printf("T%d R+ %lx\n", tid, t.cxa_guard);
  CHECK(t.in_cxa_guard);
  t.in_cxa_guard--;
}

static void After_cxa_guard_release(THREADID tid, ADDRINT pc) {
//  ADDRINT guard = g_pin_threads[tid].cxa_guard;
//  Printf("T%d R- %lx\n", tid, guard);
  IgnoreAllEnd(tid, pc);
}

static void Before_pthread_once(THREADID tid, ADDRINT pc, ADDRINT control) {
  IgnoreAllBegin(tid, pc);
//  Printf("T%d + once %lx\n", tid, control);
}
static void After_pthread_once(THREADID tid, ADDRINT pc) {
//  Printf("T%d - once \n", tid);
  IgnoreAllEnd(tid, pc);
}

void TmpCallback1(THREADID tid, ADDRINT pc) {
  Printf("%s T%d %lx\n", __FUNCTION__, tid, pc);
}
void TmpCallback2(THREADID tid, ADDRINT pc) {
  Printf("%s T%d %lx\n", __FUNCTION__, tid, pc);
}

//--------- Threads --------------------------------- {{{2
static void HandleThreadCreateBefore(THREADID tid) {
  g_thread_create_lock.Lock();
  CHECK(g_tid_of_thread_which_called_create_thread == (THREADID)-1);
  g_tid_of_thread_which_called_create_thread = tid;
  n_created_threads++;
}

static THREADID HandleThreadCreateAfter(THREADID tid, pthread_t child_ptid) {
  // Spin, waiting for last_child_tid to appear (i.e. wait for the thread to
  // actually start) so that we know the child's tid. No locks.
  while (!ATOMIC_READ(&g_pin_threads[tid].last_child_tid)) {
    YIELD();
  }

  CHECK(g_tid_of_thread_which_called_create_thread != (THREADID)-1);
  g_tid_of_thread_which_called_create_thread = -1;
  g_thread_create_lock.Unlock();

  THREADID last_child_tid = g_pin_threads[tid].last_child_tid;
  CHECK(last_child_tid);

  DumpEvent(THR_SET_PTID, last_child_tid, 0, child_ptid, 0);
  g_pin_threads[last_child_tid].my_ptid = child_ptid;
  g_pin_threads[tid].last_child_tid = 0;
  return last_child_tid;
}

static uintptr_t Wrap_pthread_create(WRAP_PARAM4) {
  HandleThreadCreateBefore(tid);

  IgnoreAllBegin(tid, pc);
  uintptr_t ret = CALL_ME_INSIDE_WRAPPER_4();
  IgnoreAllEnd(tid, pc);

  pthread_t child_ptid = *(pthread_t*)arg0;
  HandleThreadCreateAfter(tid, child_ptid);

  return ret;
}

#ifdef _MSC_VER
static void Before_CreateThread(THREADID tid, ADDRINT pc,
                                ADDRINT arg1, ADDRINT arg2,
                                ADDRINT fn, ADDRINT param,
                                ADDRINT arg5, ADDRINT arg6) {
  if (G_flags->verbosity >= 1) {
    ShowPcAndSp(__FUNCTION__, tid, pc, 0);
  }
  HandleThreadCreateBefore(tid);
}
#endif


void CallbackForThreadStart(THREADID tid, CONTEXT *ctxt,
                            INT32 flags, void *v) {
  // We can not rely on PIN_GetParentTid() since it is broken on Windows.

  if (g_pin_threads == NULL) {
    g_pin_threads = new PinThread[kMaxThreads];
  }

  bool has_parent = true;
  if (tid == 0) {
    // Main thread or we have attached to a running process.
    has_parent = false;
  } else {
    CHECK(tid > 0);
  }

  CHECK(tid < kMaxThreads);
  memset(&g_pin_threads[tid], 0, sizeof(PinThread));

  THREADID parent_tid = -1;
  if (has_parent) {
    parent_tid = g_tid_of_thread_which_called_create_thread;
    CHECK(parent_tid != (THREADID)-1);
    g_pin_threads[tid].parent_tid = parent_tid;
  }

  if (G_flags->debug_level >= 2) {
    Printf("T%d ThreadStart parent=%d child=%d\n", tid, parent_tid, tid);
  }

  if (has_parent) {
    g_pin_threads[parent_tid].last_child_tid = tid;
  }
}

#ifdef _MSC_VER
static void After_CreateThread(THREADID tid, ADDRINT pc, ADDRINT ret) {
  pthread_t child_ptid = ret;
  THREADID child_tid = HandleThreadCreateAfter(tid, child_ptid);

  {
    ScopedReentrantClientLock lock(__LINE__);
    if (g_win_handles_which_are_threads == NULL) {
      g_win_handles_which_are_threads = new hash_set<pthread_t>;
    }
    g_win_handles_which_are_threads->insert(child_ptid);
  }

  if (G_flags->verbosity >= 1) {
    ShowPcAndSp(__FUNCTION__, tid, pc, 0);
    Printf("ret: %lx; child_tid=%d\n", ret, child_tid);
  }
}
#endif
void CallbackForThreadFini(THREADID tid, const CONTEXT *ctxt,
                          INT32 code, void *v) {
  // We can not DumpEvent here,
  // due to possible deadlock with PIN's internal lock.
}

static THREADID HandleThreadJoinAfter(THREADID tid, pthread_t joined_ptid) {
  THREADID joined_tid = 0;
  for (joined_tid = 1; joined_tid < kMaxThreads; joined_tid++) {
    if (g_pin_threads[joined_tid].my_ptid == joined_ptid)
      break;
  }
  CHECK(joined_tid < kMaxThreads);
  g_pin_threads[joined_tid].my_ptid = 0;

  if (G_flags->debug_level >= 2) {
    Printf("T%d JoinAfter   parent=%d child=%d\n", tid, tid, joined_tid);
  }
  DumpEvent(THR_END, joined_tid, 0, 0, 0);
  DumpEvent(THR_JOIN_AFTER, tid, 0, joined_tid, 0);
  return joined_tid;
}

static uintptr_t Wrap_pthread_join(WRAP_PARAM4) {
  if (G_flags->debug_level >= 2)
    Printf("T%d in  pthread_join %p\n", tid, arg0);
  //DumpEvent(THR_JOIN_BEFORE, tid, 0, 0, 0);
  pthread_t joined_ptid = (pthread_t)arg0;
  uintptr_t ret = CALL_ME_INSIDE_WRAPPER_4();
  HandleThreadJoinAfter(tid, joined_ptid);
  if (G_flags->debug_level >= 2)
    Printf("T%d out pthread_join %p\n", tid, arg0);
  return ret;
}

static void *Replace_memchr(const char *s, int c, size_t n) {
  for (size_t i = 0; i < n; i++)
    if (s[i] == c) return (void*)(&s[i]);
  return NULL;
}

static uintptr_t Replace_strlen(const char *s) {
  uintptr_t res = 0;
  while (s[res])
    res++;
  return res;
}

static const void *Replace_strchr(const char *s, int c) {
  while (*s) {
    if (*s == c)
      return s;
    s++;
  }
  return NULL;
}

#ifdef _MSC_VER


uintptr_t CallStdCallFun1(CONTEXT *ctx, THREADID tid,
                         AFUNPTR f, uintptr_t arg0) {
  uintptr_t ret = 0xdeadbee1;
  PIN_CallApplicationFunction(ctx, tid,
                              CALLINGSTD_STDCALL, (AFUNPTR)(f),
                              PIN_PARG(uintptr_t), &ret,
                              PIN_PARG(uintptr_t), arg0,
                              PIN_PARG_END());
  return ret;
}

uintptr_t CallStdCallFun2(CONTEXT *ctx, THREADID tid,
                         AFUNPTR f, uintptr_t arg0, uintptr_t arg1) {
  uintptr_t ret = 0xdeadbee2;
  PIN_CallApplicationFunction(ctx, tid,
                              CALLINGSTD_STDCALL, (AFUNPTR)(f),
                              PIN_PARG(uintptr_t), &ret,
                              PIN_PARG(uintptr_t), arg0,
                              PIN_PARG(uintptr_t), arg1,
                              PIN_PARG_END());
  return ret;
}

uintptr_t CallStdCallFun3(CONTEXT *ctx, THREADID tid,
                         AFUNPTR f, uintptr_t arg0, uintptr_t arg1, 
                         uintptr_t arg2) {
  uintptr_t ret = 0xdeadbee3;
  PIN_CallApplicationFunction(ctx, tid,
                              CALLINGSTD_STDCALL, (AFUNPTR)(f),
                              PIN_PARG(uintptr_t), &ret,
                              PIN_PARG(uintptr_t), arg0,
                              PIN_PARG(uintptr_t), arg1,
                              PIN_PARG(uintptr_t), arg2,
                              PIN_PARG_END());
  return ret;
}

uintptr_t CallStdCallFun4(CONTEXT *ctx, THREADID tid,
                         AFUNPTR f, uintptr_t arg0, uintptr_t arg1,
                         uintptr_t arg2, uintptr_t arg3) {
  uintptr_t ret = 0xdeadbee4;
  PIN_CallApplicationFunction(ctx, tid,
                              CALLINGSTD_STDCALL, (AFUNPTR)(f),
                              PIN_PARG(uintptr_t), &ret,
                              PIN_PARG(uintptr_t), arg0,
                              PIN_PARG(uintptr_t), arg1,
                              PIN_PARG(uintptr_t), arg2,
                              PIN_PARG(uintptr_t), arg3,
                              PIN_PARG_END());
  return ret;
}

uintptr_t CallStdCallFun6(CONTEXT *ctx, THREADID tid,
                         AFUNPTR f, uintptr_t arg0, uintptr_t arg1,
                         uintptr_t arg2, uintptr_t arg3,
                         uintptr_t arg4, uintptr_t arg5) {
  uintptr_t ret = 0xdeadbee6;
  PIN_CallApplicationFunction(ctx, tid,
                              CALLINGSTD_STDCALL, (AFUNPTR)(f),
                              PIN_PARG(uintptr_t), &ret,
                              PIN_PARG(uintptr_t), arg0,
                              PIN_PARG(uintptr_t), arg1,
                              PIN_PARG(uintptr_t), arg2,
                              PIN_PARG(uintptr_t), arg3,
                              PIN_PARG(uintptr_t), arg4,
                              PIN_PARG(uintptr_t), arg5,
                              PIN_PARG_END());
  return ret;
}



uintptr_t Wrap_RtlInitializeCriticalSection(WRAP_PARAM4) {
//  Printf("T%d pc=%p %s: %p\n", tid, pc, __FUNCTION__+8, arg0);
  DumpEvent(LOCK_CREATE, tid, pc, arg0, 0);
  return CallStdCallFun1(ctx, tid, f, arg0);
}
uintptr_t Wrap_RtlDeleteCriticalSection(WRAP_PARAM4) {
//  Printf("T%d pc=%p %s: %p\n", tid, pc, __FUNCTION__+8, arg0);
  DumpEvent(LOCK_DESTROY, tid, pc, arg0, 0);
  return CallStdCallFun1(ctx, tid, f, arg0);
}
uintptr_t Wrap_RtlEnterCriticalSection(WRAP_PARAM4) {
//  Printf("T%d pc=%p %s: %p\n", tid, pc, __FUNCTION__+8, arg0);
  uintptr_t ret = CallStdCallFun1(ctx, tid, f, arg0);
  DumpEvent(LOCK_BEFORE, tid, pc, arg0, 0);
  DumpEvent(WRITER_LOCK, tid, pc, 0, 0);
  return ret;
}
uintptr_t Wrap_RtlTryEnterCriticalSection(WRAP_PARAM4) {
//  Printf("T%d pc=%p %s: %p\n", tid, pc, __FUNCTION__+8, arg0);
  uintptr_t ret = CallStdCallFun1(ctx, tid, f, arg0);
  if (ret) {
    DumpEvent(LOCK_BEFORE, tid, pc, arg0, 0);
    DumpEvent(WRITER_LOCK, tid, pc, 0, 0);
  }
  return ret;
}
uintptr_t Wrap_RtlLeaveCriticalSection(WRAP_PARAM4) {
//  Printf("T%d pc=%p %s: %p\n", tid, pc, __FUNCTION__+8, arg0);
  DumpEvent(UNLOCK, tid, pc, arg0, 0);
  return CallStdCallFun1(ctx, tid, f, arg0);
}

uintptr_t Wrap_SetEvent(WRAP_PARAM4) {
  //Printf("T%d before pc=%p %s: %p\n", tid, pc, __FUNCTION__+8, arg0);
  DumpEvent(SIGNAL, tid, pc, arg0, 0);
  uintptr_t ret = CallStdCallFun1(ctx, tid, f, arg0);
  //Printf("T%d after pc=%p %s: %p\n", tid, pc, __FUNCTION__+8, arg0);
  return ret;
}

uintptr_t Wrap_VirtualAlloc(WRAP_PARAM4) {
  CHECK(0);
  Printf("T%d VirtualAlloc: %p %p %p %p\n", tid, arg0, arg1, arg2, arg3);
  uintptr_t ret = CallStdCallFun4(ctx, tid, f, arg0, arg1, arg2, arg3);
  return ret;
}

uintptr_t Wrap_ZwAllocateVirtualMemory(WRAP_PARAM6) {
  // Printf("T%d >>%s(%p %p %p %p %p %p)\n", tid, __FUNCTION__, arg0, arg1, arg2, arg3, arg4, arg5);
  uintptr_t ret = CallStdCallFun6(ctx, tid, f, arg0, arg1, arg2, arg3, arg4, arg5);
  // Printf("T%d <<%s(%p %p) = %p\n", tid, __FUNCTION__, *(void**)arg1, *(void**)arg3, ret);
  if (ret == 0) {
    DumpEvent(MALLOC, tid, pc, *(uintptr_t*)arg1, *(uintptr_t*)arg3);
  }
  return ret;
}

uintptr_t Wrap_AllocateHeap(WRAP_PARAM4) {
  uintptr_t ret = CallStdCallFun3(ctx, tid, f, arg0, arg1, arg2);
  // Printf("T%d RtlAllocateHeap(%p %p %p)=%p\n", tid, arg0, arg1, arg2, ret);
  return ret;
}

uintptr_t Wrap_WaitForSingleObject(WRAP_PARAM4) {
  if (G_flags->verbosity >= 1) {
    ShowPcAndSp(__FUNCTION__, tid, pc, 0);
    Printf("arg0=%lx arg1=%lx\n", arg0, arg1);
  }
  bool is_thread_handle = false;

  {
    ScopedReentrantClientLock lock(__LINE__);
    CHECK(g_win_handles_which_are_threads);
    is_thread_handle = g_win_handles_which_are_threads->count(arg0) > 0;
    g_win_handles_which_are_threads->erase(arg0);
  }

  pthread_t joined_ptid = 0;
  if (is_thread_handle) {
    DumpEvent(THR_JOIN_BEFORE, tid, 0, arg0, 0);
    joined_ptid = arg0;
  }


  //Printf("T%d before pc=%p %s: %p\n", tid, pc, __FUNCTION__+8, arg0, arg1);
  uintptr_t ret = CallStdCallFun2(ctx, tid, f, arg0, arg1);
  //Printf("T%d after pc=%p %s: %p\n", tid, pc, __FUNCTION__+8, arg0, arg1);
  DumpEvent(WAIT_BEFORE, tid, pc, arg0, 0);
  DumpEvent(WAIT_AFTER, tid, pc, 0, 0);

  if (is_thread_handle) {
    THREADID joined_tid = HandleThreadJoinAfter(tid, joined_ptid);
  }

  return ret;
}

#endif  // _MSC_VER

//--------- main() --------------------------------- {{{2
void Before_main(THREADID tid, ADDRINT pc, ADDRINT argc, ADDRINT argv) {
  CHECK(tid == 0);
  main_entered = true;
}

void After_main(THREADID tid, ADDRINT pc) {
  CHECK(tid == 0);
  main_exited = true;
}

//--------- memory allocation ---------------------- {{{2
void Before_mmap(THREADID tid, ADDRINT pc, ADDRINT start, ADDRINT len) {
  PinThread &t = g_pin_threads[tid];
  CHECK(t.last_mmap_size == 0);
  t.last_mmap_size = len;
}
void After_mmap(THREADID tid, ADDRINT pc, ADDRINT ret) {
  PinThread &t = g_pin_threads[tid];
  if (ret != (ADDRINT)-1L) {
    size_t size = t.last_mmap_size;
    DumpEvent(MALLOC, tid, pc, ret, size);
  }
  t.last_mmap_size = 0;
}

//-------- Routines and stack ---------------------- {{{2
static void UpdateCallStack(THREADID tid, ADDRINT sp) {
  PinThread &t = g_pin_threads[tid];
  while (t.shadow_stack.size() > 0 && sp >= t.shadow_stack.back().sp) {
    DumpEvent(RTN_EXIT, tid, 0, 0, 0);
    t.shadow_stack.pop_back();
    if (DEB_PR) {
      Printf("POP SHADOW STACK\n");
      PrintShadowStack(tid);
    }
  }
}

//static void InsertAfterEvent_RoutineExit(THREADID tid, ADDRINT pc, ADDRINT sp) {
  // PinThread &t = g_pin_threads[tid];
  // UpdateCallStack(tid, sp);
//}

void InsertBeforeEvent_Call(THREADID tid, ADDRINT pc, ADDRINT target, ADDRINT sp) {
  DebugOnlyShowPcAndSp(__FUNCTION__, tid, pc, sp);
  UpdateCallStack(tid, sp);
  DumpEvent(RTN_CALL, tid, pc, target, 0);
  PinThread &t = g_pin_threads[tid];
  if (t.shadow_stack.size() > 0) {
    t.shadow_stack.back().pc = pc;
  }
  t.shadow_stack.push_back(StackFrame(target, sp));
  if (DEB_PR) {
    PrintShadowStack(tid);
  }
  if (DEBUG_MODE && (G_flags->verbosity >= 2)) {
    ShowPcAndSp("CALL: ", tid, target, sp);
  }
}

static void InsertAfterEvent_SpUpdate(THREADID tid, ADDRINT pc, ADDRINT sp) {
  DebugOnlyShowPcAndSp(__FUNCTION__, tid, pc, sp);
}

static void HandleSblockEntry(THREADID tid, ADDRINT pc, ADDRINT sp) {
  DebugOnlyShowPcAndSp(__FUNCTION__, tid, pc, sp);
  UpdateCallStack(tid, sp);
  DumpEvent(SBLOCK_ENTER, tid, pc, 0, 0);
}

static void InsertBeforeEvent_SblockEntry(THREADID tid, ADDRINT pc, ADDRINT sp) {
  HandleSblockEntry(tid, pc, sp);
}

static void InsertBeforeEvent_RoutineEntry(THREADID tid, ADDRINT pc,
                                    ADDRINT sp, Routine *routine) {
  HandleSblockEntry(tid, pc, sp);
}


//---------- Memory accesses -------------------------- {{{2
static void InsertBeforeEvent_MemoryRead(THREADID tid, ADDRINT pc,
                                         ADDRINT a, ADDRINT size) {
  if (DEBUG_MODE && a == G_flags->trace_addr) {
    Printf("T%d %s %lx\n", tid, __FUNCTION__, a);
  }
  if (size > 0) {
    DumpEvent(READ, tid, pc, a, size);
  }
  dyn_read_count++;
}


static void InsertBeforeEvent_MemoryWrite(THREADID tid, ADDRINT pc,
                                          ADDRINT a, ADDRINT size) {
  if (DEBUG_MODE && a == G_flags->trace_addr) {
    Printf("T%d %s %lx\n", tid, __FUNCTION__, a);
  }
  if (size > 0) {
    DumpEvent(WRITE, tid, pc, a, size);
  }
  dyn_write_count++;
}

//---------- I/O; exit------------------------------- {{{2
static const uintptr_t kIOMagic = 0x1234c678;

static void Before_SignallingIOCall(THREADID tid, ADDRINT pc) {
  DumpEvent(SIGNAL, tid, pc, kIOMagic, 0);
}

static void After_WaitingIOCall(THREADID tid, ADDRINT pc) {
  DumpEvent(WAIT_BEFORE, tid, pc, kIOMagic, 0);
  DumpEvent(WAIT_AFTER, tid, pc, 0, 0);
}

static const uintptr_t kAtexitMagic = 0x9876f432;

static void On_atexit(THREADID tid, ADDRINT pc) {
  DumpEvent(SIGNAL, tid, pc, kAtexitMagic, 0);
}

static void On_exit(THREADID tid, ADDRINT pc) {
  DumpEvent(WAIT_BEFORE, tid, pc, kAtexitMagic, 0);
  DumpEvent(WAIT_AFTER, tid, pc, 0, 0);
}

//---------- Synchronization -------------------------- {{{2
// locks
static void Before_pthread_unlock(THREADID tid, ADDRINT pc, ADDRINT mu) {
  DumpEvent(UNLOCK, tid, pc, mu, 0);
}
static void Before_pthread_spin_unlock(THREADID tid, ADDRINT pc, ADDRINT mu) {
  DumpEvent(UNLOCK_OR_INIT, tid, pc, mu, 0);
}

static void Before_pthread_lock(THREADID tid, ADDRINT pc, ADDRINT mu) {
  DumpEvent(LOCK_BEFORE, tid, pc, mu, 0);
}

static void After_pthread_lock(THREADID tid, ADDRINT pc, ADDRINT unused) {
  DumpEvent(WRITER_LOCK, tid, pc, 0, 0);
}

static void After_pthread_trylock(THREADID tid, ADDRINT pc, ADDRINT ret) {
  if (ret == 0)
    DumpEvent(WRITER_LOCK, tid, pc, 0, 0);
}

static void After_pthread_rdlock(THREADID tid, ADDRINT pc, ADDRINT unused) {
  DumpEvent(READER_LOCK, tid, pc, 0, 0);
}

static void After_pthread_tryrdlock(THREADID tid, ADDRINT pc, ADDRINT ret) {
  if (ret == 0)
    DumpEvent(READER_LOCK, tid, pc, 0, 0);
}

static void Before_pthread_mutex_init(THREADID tid, ADDRINT pc, ADDRINT mu) {
  DumpEvent(LOCK_CREATE, tid, pc, mu, 0);
}
static void Before_pthread_rwlock_init(THREADID tid, ADDRINT pc, ADDRINT mu) {
  DumpEvent(LOCK_CREATE, tid, pc, mu, 0);
}
static void Before_pthread_spin_init(THREADID tid, ADDRINT pc, ADDRINT mu) {
  DumpEvent(UNLOCK_OR_INIT, tid, pc, mu, 0);
}

static void Before_pthread_mutex_destroy(THREADID tid, ADDRINT pc, ADDRINT mu) {
  DumpEvent(LOCK_DESTROY, tid, pc, mu, 0);
}
static void Before_pthread_rwlock_destroy(THREADID tid, ADDRINT pc, ADDRINT mu) {
  DumpEvent(LOCK_DESTROY, tid, pc, mu, 0);
}
static void Before_pthread_spin_destroy(THREADID tid, ADDRINT pc, ADDRINT mu) {
  DumpEvent(LOCK_DESTROY, tid, pc, mu, 0);
}

// barrier
static void Before_pthread_barrier_wait(THREADID tid, ADDRINT pc,
                                        ADDRINT barrier) {
  DumpEvent(BARRIER_BEFORE, tid, pc, barrier, 0);
}
static void After_pthread_barrier_wait(THREADID tid, ADDRINT pc) {
  DumpEvent(BARRIER_AFTER, tid, pc, 0, 0);
}

// condvar
static void Before_pthread_cond_signal(THREADID tid, ADDRINT pc, ADDRINT cv) {
  DumpEvent(SIGNAL, tid, pc, cv, 0);
}
static void Before_pthread_cond_wait(THREADID tid, ADDRINT pc,
                                     ADDRINT cv, ADDRINT mu) {
  DumpEvent(WAIT_BEFORE, tid, pc, cv, 0);
}
static void After_pthread_cond_wait(THREADID tid, ADDRINT pc) {
  DumpEvent(WAIT_AFTER, tid, pc, 0, 0);
}
static void After_pthread_cond_timedwait(THREADID tid, ADDRINT pc,
                                         ADDRINT ret) {
  if (ret == 0) {
    DumpEvent(WAIT_AFTER, tid, pc, 0, 0);
  } else {
    DumpEvent(TWAIT_AFTER, tid, pc, 0, 0);
  }
}

// sem
static void After_sem_open(THREADID tid, ADDRINT pc, ADDRINT ret) {
  // TODO(kcc): need to handle it more precise?
  DumpEvent(SIGNAL, tid, pc, ret, 0);
}
static void Before_sem_post(THREADID tid, ADDRINT pc, ADDRINT sem) {
  DumpEvent(SIGNAL, tid, pc, sem, 0);
}
static void Before_sem_wait(THREADID tid, ADDRINT pc, ADDRINT sem) {
  DumpEvent(WAIT_BEFORE, tid, pc, sem, 0);
}
static void After_sem_wait(THREADID tid, ADDRINT pc) {
  DumpEvent(WAIT_AFTER, tid, pc, 0, 0);
}
static void After_sem_trywait(THREADID tid, ADDRINT pc, ADDRINT ret) {
  if (ret == 0) {
    DumpEvent(WAIT_AFTER, tid, pc, 0, 0);
  } else {
    DumpEvent(TWAIT_AFTER, tid, pc, 0, 0);
  }
}

//---------- Annotations -------------------------- {{{2
static void On_AnnotateBenignRace(THREADID tid, ADDRINT pc,
                                  ADDRINT file, ADDRINT line,
                                  ADDRINT a, ADDRINT descr) {
  DumpEvent(EXPECT_RACE, tid, descr, a, 1);
}

static void On_AnnotateExpectRace(THREADID tid, ADDRINT pc,
                                  ADDRINT file, ADDRINT line,
                                  ADDRINT a, ADDRINT descr) {
  DumpEvent(EXPECT_RACE, tid, descr, a, 0);
}

static void On_AnnotateTraceMemory(THREADID tid, ADDRINT pc,
                                   ADDRINT file, ADDRINT line,
                                   ADDRINT a) {
  DumpEvent(TRACE_MEM, tid, pc, a, 0);
}

static void On_AnnotateNoOp(THREADID tid, ADDRINT pc,
                            ADDRINT file, ADDRINT line) {
  Printf("%s T%d\n", __FUNCTION__, tid);
//  DumpEvent(STACK_TRACE, tid, pc, 0, 0);
//  PrintShadowStack(tid);
}

static void On_AnnotateCondVarSignal(THREADID tid, ADDRINT pc,
                                     ADDRINT file, ADDRINT line, ADDRINT obj) {
  DumpEvent(SIGNAL, tid, pc, obj, 0);
}

static void On_AnnotateCondVarWait(THREADID tid, ADDRINT pc,
                                   ADDRINT file, ADDRINT line, ADDRINT obj) {
  DumpEvent(WAIT_BEFORE, tid, pc, obj, 0);
  DumpEvent(WAIT_AFTER, tid, pc, obj, 0);
}


static void On_AnnotateIgnoreReadsBegin(THREADID tid, ADDRINT pc,
                                        ADDRINT file, ADDRINT line) {
  DumpEvent(IGNORE_READS_BEG, tid, pc, 0, 0);
}
static void On_AnnotateIgnoreReadsEnd(THREADID tid, ADDRINT pc,
                                      ADDRINT file, ADDRINT line) {
  DumpEvent(IGNORE_READS_END, tid, pc, 0, 0);
}
static void On_AnnotateIgnoreWritesBegin(THREADID tid, ADDRINT pc,
                                         ADDRINT file, ADDRINT line) {
  DumpEvent(IGNORE_WRITES_BEG, tid, pc, 0, 0);
}
static void On_AnnotateIgnoreWritesEnd(THREADID tid, ADDRINT pc,
                                       ADDRINT file, ADDRINT line) {
  DumpEvent(IGNORE_WRITES_END, tid, pc, 0, 0);
}

static void On_AnnotatePublishMemoryRange(THREADID tid, ADDRINT pc,
                                          ADDRINT file, ADDRINT line,
                                          ADDRINT a, ADDRINT size) {
  DumpEvent(PUBLISH_RANGE, tid, pc, a, size);
}

static void On_AnnotateUnpublishMemoryRange(THREADID tid, ADDRINT pc,
                                          ADDRINT file, ADDRINT line,
                                          ADDRINT a, ADDRINT size) {
//  Printf("T%d %s %lx %lx\n", tid, __FUNCTION__, a, size);
  DumpEvent(UNPUBLISH_RANGE, tid, pc, a, size);
}


static void On_AnnotateMutexIsUsedAsCondVar(THREADID tid, ADDRINT pc,
                                            ADDRINT file, ADDRINT line,
                                            ADDRINT mu) {
  DumpEvent(HB_LOCK, tid, pc, mu, 0);
}

static void On_AnnotatePCQCreate(THREADID tid, ADDRINT pc,
                                 ADDRINT file, ADDRINT line,
                                 ADDRINT pcq) {
  DumpEvent(PCQ_CREATE, tid, pc, pcq, 0);
}

static void On_AnnotatePCQDestroy(THREADID tid, ADDRINT pc,
                                  ADDRINT file, ADDRINT line,
                                  ADDRINT pcq) {
  DumpEvent(PCQ_DESTROY, tid, pc, pcq, 0);
}

static void On_AnnotatePCQPut(THREADID tid, ADDRINT pc,
                              ADDRINT file, ADDRINT line,
                              ADDRINT pcq) {
  DumpEvent(PCQ_PUT, tid, pc, pcq, 0);
}

static void On_AnnotatePCQGet(THREADID tid, ADDRINT pc,
                              ADDRINT file, ADDRINT line,
                              ADDRINT pcq) {
  DumpEvent(PCQ_GET, tid, pc, pcq, 0);
}

//--------- Instrumentation ----------------------- {{{1
static bool IgnoreImage(IMG img) {
  string name = IMG_Name(img);
  if (name.find("/ld-") != string::npos)
    return true;
  return false;
}

static bool IgnoreRtn(RTN rtn) {
  CHECK(rtn != RTN_Invalid());
  ADDRINT rtn_address = RTN_Address(rtn);
  if (ThreadSanitizerWantToInstrumentSblock(rtn_address) == false)
    return true;
  return false;
}

static void InstrumentRead(INS ins) {
  INS_InsertCall(ins, IPOINT_BEFORE,
                 (AFUNPTR)InsertBeforeEvent_MemoryRead,
                 IARG_THREAD_ID, IARG_INST_PTR,
                 IARG_MEMORYREAD_EA, IARG_MEMORYREAD_SIZE,
                 IARG_END);
}

static void InstrumentWrite(INS ins) {
  INS_InsertCall(ins, IPOINT_BEFORE,
                 (AFUNPTR)InsertBeforeEvent_MemoryWrite,
                 IARG_THREAD_ID, IARG_INST_PTR,
                 IARG_MEMORYWRITE_EA, IARG_MEMORYWRITE_SIZE,
                 IARG_END);
}

static void InstrumentRead2(INS ins) {
  INS_InsertCall(ins, IPOINT_BEFORE,
                 (AFUNPTR)InsertBeforeEvent_MemoryRead,
                 IARG_THREAD_ID, IARG_INST_PTR,
                 IARG_MEMORYREAD2_EA, IARG_MEMORYREAD_SIZE,
                 IARG_END);
}

static void InstrumentBbl(BBL bbl, RTN rtn, bool ignore_memory) {
  INS tail = BBL_InsTail(bbl);

  // All memory reads/writes
  for( INS ins = BBL_InsHead(bbl);
       !ignore_memory && INS_Valid(ins);
       ins = INS_Next(ins) ) {
    if (ins != tail) {
      CHECK(!INS_IsRet(ins));
      CHECK(!INS_IsProcedureCall(ins));
    }
    // bool is_stack = INS_IsStackRead(ins) || INS_IsStackWrite(ins);
    bool is_atomic = INS_IsAtomicUpdate(ins);
    bool is_read  = INS_IsMemoryRead(ins);
    bool is_read2 = INS_HasMemoryRead2(ins);
    bool is_write = INS_IsMemoryWrite(ins);

    // SP update.
    if (INS_RegWContain(ins, REG_STACK_PTR)) {
      INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)InsertAfterEvent_SpUpdate,
                     IARG_THREAD_ID, IARG_INST_PTR,
                     IARG_REG_VALUE, REG_STACK_PTR,
                     IARG_END);

    }
    if (is_atomic) continue;
    if (is_read)  InstrumentRead(ins);
    if (is_read2) InstrumentRead2(ins);
    if (is_write) InstrumentWrite(ins);
  }

  // Call.
  if (INS_IsProcedureCall(tail) && !INS_IsSyscall(tail)) {
    INS_InsertCall(tail, IPOINT_BEFORE,
                   (AFUNPTR)InsertBeforeEvent_Call,
                   IARG_THREAD_ID, IARG_INST_PTR,
                   IARG_BRANCH_TARGET_ADDR,
                   IARG_REG_VALUE, REG_STACK_PTR,
                   IARG_END);
  }

//  if (INS_IsRet(tail)) {
//    INS_InsertCall(tail, IPOINT_BEFORE,
//                   (AFUNPTR)InsertAfterEvent_RoutineExit,
//                   IARG_THREAD_ID, IARG_INST_PTR,
//                   IARG_REG_VALUE, REG_STACK_PTR,
//                   IARG_END);
//  }
}

void CallbackForTRACE(TRACE trace, void *v) {
  RTN rtn = TRACE_Rtn(trace);
  bool ignore_memory = false;
  string img_name = "<>";
  string rtn_name = "<>";
  if (RTN_Valid(rtn)) {
    SEC sec = RTN_Sec(rtn);
    IMG img = SEC_Img(sec);
    rtn_name = RTN_Name(rtn);
    img_name = IMG_Name(img);

    if (IgnoreImage(img)) {
      // Printf("Ignoring memory accesses in %s\n", IMG_Name(img).c_str());
      ignore_memory = true;
    } else if (IgnoreRtn(rtn)) {
      ignore_memory = true;
    }
  }

  // Handle the head of the trace
  uintptr_t address = TRACE_Address(trace);
  map<uintptr_t, Routine*>::iterator it =
      routine_address_map->find(address);
  if (it != routine_address_map->end()) {
    // If this trace is a routine entrace, place RTN_ENTER
    Routine *routine = it->second;
    TRACE_InsertCall(trace, IPOINT_BEFORE,
                     (AFUNPTR)InsertBeforeEvent_RoutineEntry,
                     IARG_THREAD_ID,
                     IARG_INST_PTR,
                     IARG_REG_VALUE, REG_STACK_PTR,
                     IARG_PTR, routine,
                     IARG_END);
    // Printf("TRACE: head of rtn %s\n", routine->name());
  } else {
    // Otherwise place SBLOCK_ENTER (only if we are tracking access history)
    TRACE_InsertCall(trace, IPOINT_BEFORE,
                     (AFUNPTR)InsertBeforeEvent_SblockEntry,
                     IARG_THREAD_ID, IARG_INST_PTR,
                     IARG_REG_VALUE, REG_STACK_PTR,
                     IARG_END);
  }

  for(BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
    InstrumentBbl(bbl, rtn, ignore_memory);
  }
}


#define INSERT_FN_HELPER(point, name, rtn, to_insert, ...) \
    RTN_Open(rtn); \
    if (G_flags->verbosity >= 2) Printf("RTN: Inserting %-50s (%s) %s (%s) img: %s\n", \
    #to_insert, #point, RTN_Name(rtn).c_str(), name, IMG_Name(img).c_str());\
    RTN_InsertCall(rtn, point, (AFUNPTR)to_insert, IARG_THREAD_ID, \
                   IARG_INST_PTR, __VA_ARGS__, IARG_END);\
    RTN_Close(rtn); \

#define INSERT_FN(point, name, to_insert, ...) \
  while (RtnMatchesName(rtn_name, name)) {\
    INSERT_FN_HELPER(point, name, rtn, to_insert, __VA_ARGS__); \
    break;\
  }\


#define INSERT_BEFORE_FN(name, to_insert, ...) \
    INSERT_FN(IPOINT_BEFORE, name, to_insert, __VA_ARGS__)

#define INSERT_BEFORE_0(name, to_insert) \
    INSERT_BEFORE_FN(name, to_insert, IARG_END);

#define INSERT_BEFORE_1(name, to_insert) \
    INSERT_BEFORE_FN(name, to_insert, \
                     IARG_FUNCARG_ENTRYPOINT_VALUE, 0)

#define INSERT_BEFORE_2(name, to_insert) \
    INSERT_BEFORE_FN(name, to_insert, \
                     IARG_FUNCARG_ENTRYPOINT_VALUE, 0, \
                     IARG_FUNCARG_ENTRYPOINT_VALUE, 1)

#define INSERT_BEFORE_3(name, to_insert) \
    INSERT_BEFORE_FN(name, to_insert, \
                     IARG_FUNCARG_ENTRYPOINT_VALUE, 0, \
                     IARG_FUNCARG_ENTRYPOINT_VALUE, 1, \
                     IARG_FUNCARG_ENTRYPOINT_VALUE, 2)

#define INSERT_BEFORE_4(name, to_insert) \
    INSERT_BEFORE_FN(name, to_insert, \
                     IARG_FUNCARG_ENTRYPOINT_VALUE, 0, \
                     IARG_FUNCARG_ENTRYPOINT_VALUE, 1, \
                     IARG_FUNCARG_ENTRYPOINT_VALUE, 2, \
                     IARG_FUNCARG_ENTRYPOINT_VALUE, 3)

#define INSERT_BEFORE_6(name, to_insert) \
    INSERT_BEFORE_FN(name, to_insert, \
                     IARG_FUNCARG_ENTRYPOINT_VALUE, 0, \
                     IARG_FUNCARG_ENTRYPOINT_VALUE, 1, \
                     IARG_FUNCARG_ENTRYPOINT_VALUE, 2, \
                     IARG_FUNCARG_ENTRYPOINT_VALUE, 3, \
                     IARG_FUNCARG_ENTRYPOINT_VALUE, 4, \
                     IARG_FUNCARG_ENTRYPOINT_VALUE, 5)

#define INSERT_AFTER_FN(name, to_insert, ...) \
    INSERT_FN(IPOINT_AFTER, name, to_insert, __VA_ARGS__)

#define INSERT_AFTER_0(name, to_insert) \
    INSERT_AFTER_FN(name, to_insert, IARG_END)

#define INSERT_AFTER_1(name, to_insert) \
    INSERT_AFTER_FN(name, to_insert, IARG_FUNCRET_EXITPOINT_VALUE)


#define INSERT_FN_SLOW(point, name, to_insert, ...) \
  while (RTN_Valid((rtn = RTN_FindByName(img, name)))) { \
    INSERT_FN_HELPER(point, name, rtn, to_insert, __VA_ARGS__); \
    break; \
  }

#define INSERT_BEFORE_SLOW_1(name, to_insert) \
    INSERT_FN_SLOW(IPOINT_BEFORE, name, to_insert, \
                   IARG_FUNCARG_ENTRYPOINT_VALUE, 0)

#define INSERT_BEFORE_SLOW_2(name, to_insert) \
    INSERT_FN_SLOW(IPOINT_BEFORE, name, to_insert, \
                   IARG_FUNCARG_ENTRYPOINT_VALUE, 0, \
                   IARG_FUNCARG_ENTRYPOINT_VALUE, 1)

#define INSERT_AFTER_SLOW_0(name, to_insert) \
    INSERT_FN_SLOW(IPOINT_AFTER, name, to_insert, IARG_END)

#define INSERT_AFTER_SLOW_1(name, to_insert) \
    INSERT_FN_SLOW(IPOINT_AFTER, name, to_insert, IARG_FUNCRET_EXITPOINT_VALUE)

// I wonder if we can do this with templates???

#define DEBUG_REPLACE_RTN (0)

#define DEFINE_REPLACEMENT_RTN_1_1(name, RET_T, ARG1_T, before, after)       \
RET_T Wrap_##name(CONTEXT *context, AFUNPTR orig_func, ARG1_T arg1) {     \
  if (DEBUG_REPLACE_RTN) {                                             \
    Printf("->%s: orig=0x%p arg1=%p\n", __FUNCTION__, orig_func,             \
           (void*)arg1);                                                     \
  }                                                                          \
  void *ret;                                                                 \
  THREADID tid = PIN_ThreadId();                                             \
  uintptr_t pc = (uintptr_t)orig_func;                                       \
  {before;}                                                                  \
  PIN_CallApplicationFunction(context, tid,                                  \
                              CALLINGSTD_DEFAULT, orig_func,                 \
                              PIN_PARG(RET_T), &ret,                         \
                              PIN_PARG(ARG1_T), arg1,                        \
                              PIN_PARG_END());                               \
  if (DEBUG_REPLACE_RTN) {                                             \
    Printf("<-%s: ret=0x%p\n", __FUNCTION__, (void*)ret);                    \
  }                                                                          \
  {after;}                                                                   \
  return ret;                                                                \
}

#define DEFINE_REPLACEMENT_RTN_1_2(name, RET_T, ARG1_T, ARG2_T, before, after) \
RET_T Wrap_##name(CONTEXT *context, AFUNPTR orig_func,                    \
                     ARG1_T arg1, ARG2_T arg2) {                             \
  if (DEBUG_REPLACE_RTN) {                                             \
    Printf("->%s: orig=0x%p arg1=%p\n", __FUNCTION__, orig_func,             \
           (void*)arg1, (void*)arg2);                                        \
  }                                                                          \
  void *ret;                                                                 \
  THREADID tid = PIN_ThreadId();                                             \
  uintptr_t pc = (uintptr_t)orig_func;                                       \
  {before;}                                                                  \
  PIN_CallApplicationFunction(context, PIN_ThreadId(),                       \
                              CALLINGSTD_DEFAULT, orig_func,                 \
                              PIN_PARG(RET_T), &ret,                         \
                              PIN_PARG(ARG1_T), arg1,                        \
                              PIN_PARG(ARG1_T), arg2,                        \
                              PIN_PARG_END());                               \
  if (DEBUG_REPLACE_RTN) {                                             \
    Printf("<-%s: ret=0x%p\n", __FUNCTION__, (void*)ret);                    \
  }                                                                          \
  {after;}                                                                   \
  return ret;                                                                \
}


#define DEFINE_REPLACEMENT_RTN_0_1(name, ARG1_T, do_before, do_after)        \
void Wrap_##name(CONTEXT *context, AFUNPTR orig_func, ARG1_T arg1) {      \
  if (DEBUG_REPLACE_RTN) {                                             \
    Printf("->%s: orig=0x%p arg1=%p\n", __FUNCTION__, orig_func,             \
           (void*)arg1);                                                     \
  }                                                                          \
  THREADID tid = PIN_ThreadId();                                             \
  uintptr_t pc = (uintptr_t)orig_func;                                       \
  {do_before;}                                                               \
  PIN_CallApplicationFunction(context, PIN_ThreadId(),                       \
                              CALLINGSTD_DEFAULT, orig_func,                 \
                              PIN_PARG(void),                                \
                              PIN_PARG(ARG1_T), arg1,                        \
                              PIN_PARG_END());                               \
  if (DEBUG_REPLACE_RTN) {                                             \
    Printf("<-%s\n", __FUNCTION__);                                          \
  }                                                                          \
  {do_after;}                                                                \
}

#define REPLACE_RTN_1(name, RET_T, ARG1_T) do {                              \
  PROTO proto = PROTO_Allocate(PIN_PARG(RET_T),                              \
                               CALLINGSTD_DEFAULT,                           \
                               #name,                                        \
                               PIN_PARG(ARG1_T),                             \
                               PIN_PARG_END());                              \
  RTN_ReplaceSignature(                                                      \
      rtn, AFUNPTR(Wrap_##name),                                          \
      IARG_PROTOTYPE, proto,                                                 \
      IARG_CONTEXT,                                                          \
      IARG_ORIG_FUNCPTR,                                                     \
      IARG_FUNCARG_ENTRYPOINT_VALUE, 0,                                      \
      IARG_END);                                                             \
  PROTO_Free(proto);                                                         \
  if (G_flags->verbosity >= 1) {                                             \
    Printf("%s: img=%s rtn=%s\n", __FUNCTION__,                              \
         img_name.c_str(), rtn_name.c_str());                                \
  }                                                                          \
} while(0)

#define REPLACE_RTN_2(name, RET_T, ARG1_T, ARG2_T) do {                      \
  PROTO proto = PROTO_Allocate(PIN_PARG(RET_T),                              \
                               CALLINGSTD_DEFAULT,                           \
                               #name,                                        \
                               PIN_PARG(ARG1_T),                             \
                               PIN_PARG(ARG2_T),                             \
                               PIN_PARG_END());                              \
  RTN_ReplaceSignature(                                                      \
      rtn, AFUNPTR(Wrap_##name),                                          \
      IARG_PROTOTYPE, proto,                                                 \
      IARG_CONTEXT,                                                          \
      IARG_ORIG_FUNCPTR,                                                     \
      IARG_FUNCARG_ENTRYPOINT_VALUE, 0,                                      \
      IARG_FUNCARG_ENTRYPOINT_VALUE, 1,                                      \
      IARG_END);                                                             \
  PROTO_Free(proto);                                                         \
  if (G_flags->verbosity >= 1) {                                             \
    Printf("%s: img=%s rtn=%s\n", __FUNCTION__,                              \
         img_name.c_str(), rtn_name.c_str());                                \
  }                                                                          \
} while(0)


#define MATCH_NAME_AND_REPLACE_RTN_1(name, RET_T, ARG1_T) do {               \
  if (RtnMatchesName(rtn_name, #name)) {                                     \
    REPLACE_RTN_1(name, RET_T, ARG1_T);                                      \
  }                                                                          \
} while(0)

#define MATCH_NAME_AND_REPLACE_RTN_2(name, RET_T, ARG1_T, ARG2_T) do {       \
  if (RtnMatchesName(rtn_name, #name)) {                                     \
    REPLACE_RTN_2(name, RET_T, ARG1_T, ARG2_T);                              \
  }                                                                          \
} while(0)



DEFINE_REPLACEMENT_RTN_1_1(
  malloc,
  void *,
  size_t,
  {
    IgnoreAllBegin(tid, pc);
  },
  {
    DumpEvent(MALLOC, tid, pc, (uintptr_t)ret, arg1);
    IgnoreAllEnd(tid, pc);
  }
)

DEFINE_REPLACEMENT_RTN_1_2(
  calloc,
  void *,
  size_t,
  size_t,
  {
    IgnoreAllBegin(tid, pc);
  },
  {
    DumpEvent(MALLOC, tid, pc, (uintptr_t)ret, arg1 * arg2);
    IgnoreAllEnd(tid, pc);
  }
)

DEFINE_REPLACEMENT_RTN_0_1(
  free,
  void *,
  {
    IgnoreAllBegin(tid, pc);
    DumpEvent(FREE, PIN_ThreadId(), (uintptr_t)orig_func, (uintptr_t)arg1, 0);
  },
  {
    IgnoreAllEnd(tid, pc);
  }
)


#ifdef _MSC_VER
void WrapStdCallFunc1(RTN rtn, char *name, AFUNPTR replacement_func) {
  if (RTN_Valid(rtn) && RtnMatchesName(RTN_Name(rtn), name)) {
    Printf("RTN_ReplaceSignature on %s\n", name);
    PROTO proto = PROTO_Allocate(PIN_PARG(uintptr_t),
                                 CALLINGSTD_STDCALL,
                                 "proto",
                                 PIN_PARG(uintptr_t),
                                 PIN_PARG_END());
    RTN_ReplaceSignature(rtn,
                         AFUNPTR(replacement_func),
                         IARG_PROTOTYPE, proto,
                         IARG_THREAD_ID,
                         IARG_INST_PTR,
                         IARG_CONTEXT,
                         IARG_ORIG_FUNCPTR,
                         IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                         IARG_END);
    PROTO_Free(proto);
  }
}

void WrapStdCallFunc2(RTN rtn, char *name, AFUNPTR replacement_func) {
  if (RTN_Valid(rtn) && RtnMatchesName(RTN_Name(rtn), name)) {
    Printf("RTN_ReplaceSignature on %s\n", name);
    PROTO proto = PROTO_Allocate(PIN_PARG(uintptr_t),
                                 CALLINGSTD_STDCALL,
                                 "proto",
                                 PIN_PARG(uintptr_t),
                                 PIN_PARG(uintptr_t),
                                 PIN_PARG_END());
    RTN_ReplaceSignature(rtn,
                         AFUNPTR(replacement_func),
                         IARG_PROTOTYPE, proto,
                         IARG_THREAD_ID,
                         IARG_INST_PTR,
                         IARG_CONTEXT,
                         IARG_ORIG_FUNCPTR,
                         IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                         IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
                         IARG_END);
    PROTO_Free(proto);
  }
}

void WrapStdCallFunc3(RTN rtn, char *name, AFUNPTR replacement_func) {
  if (RTN_Valid(rtn) && RtnMatchesName(RTN_Name(rtn), name)) {
    Printf("RTN_ReplaceSignature on %s\n", name);
    PROTO proto = PROTO_Allocate(PIN_PARG(uintptr_t),
                                 CALLINGSTD_STDCALL,
                                 "proto",
                                 PIN_PARG(uintptr_t),
                                 PIN_PARG(uintptr_t),
                                 PIN_PARG(uintptr_t),
                                 PIN_PARG_END());
    RTN_ReplaceSignature(rtn,
                         AFUNPTR(replacement_func),
                         IARG_PROTOTYPE, proto,
                         IARG_THREAD_ID,
                         IARG_INST_PTR,
                         IARG_CONTEXT,
                         IARG_ORIG_FUNCPTR,
                         IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                         IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
                         IARG_FUNCARG_ENTRYPOINT_VALUE, 2,
                         IARG_END);
    PROTO_Free(proto);
  }
}

void WrapStdCallFunc4(RTN rtn, char *name, AFUNPTR replacement_func) {
  if (RTN_Valid(rtn) && RtnMatchesName(RTN_Name(rtn), name)) {
    Printf("RTN_ReplaceSignature on %s\n", name);
    PROTO proto = PROTO_Allocate(PIN_PARG(uintptr_t),
                                 CALLINGSTD_STDCALL,
                                 "proto",
                                 PIN_PARG(uintptr_t),
                                 PIN_PARG(uintptr_t),
                                 PIN_PARG(uintptr_t),
                                 PIN_PARG(uintptr_t),
                                 PIN_PARG_END());
    RTN_ReplaceSignature(rtn,
                         AFUNPTR(replacement_func),
                         IARG_PROTOTYPE, proto,
                         IARG_THREAD_ID,
                         IARG_INST_PTR,
                         IARG_CONTEXT,
                         IARG_ORIG_FUNCPTR,
                         IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                         IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
                         IARG_FUNCARG_ENTRYPOINT_VALUE, 2,
                         IARG_FUNCARG_ENTRYPOINT_VALUE, 3,
                         IARG_END);
    PROTO_Free(proto);
  }
}

void WrapStdCallFunc6(RTN rtn, char *name, AFUNPTR replacement_func) {
  if (RTN_Valid(rtn) && RtnMatchesName(RTN_Name(rtn), name)) {
    Printf("RTN_ReplaceSignature on %s\n", name);
    PROTO proto = PROTO_Allocate(PIN_PARG(uintptr_t),
                                 CALLINGSTD_STDCALL,
                                 "proto",
                                 PIN_PARG(uintptr_t),
                                 PIN_PARG(uintptr_t),
                                 PIN_PARG(uintptr_t),
                                 PIN_PARG(uintptr_t),
                                 PIN_PARG(uintptr_t),
                                 PIN_PARG(uintptr_t),
                                 PIN_PARG_END());
    RTN_ReplaceSignature(rtn,
                         AFUNPTR(replacement_func),
                         IARG_PROTOTYPE, proto,
                         IARG_THREAD_ID,
                         IARG_INST_PTR,
                         IARG_CONTEXT,
                         IARG_ORIG_FUNCPTR,
                         IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                         IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
                         IARG_FUNCARG_ENTRYPOINT_VALUE, 2,
                         IARG_FUNCARG_ENTRYPOINT_VALUE, 3,
                         IARG_FUNCARG_ENTRYPOINT_VALUE, 4,
                         IARG_FUNCARG_ENTRYPOINT_VALUE, 5,
                         IARG_END);
    PROTO_Free(proto);
  }
}


#endif

static void MaybeInstrumentOneRoutine(IMG img, RTN rtn) {
  if (IgnoreImage(img)) {
    return;
  }
  string rtn_name = RTN_Name(rtn);
  string img_name = IMG_Name(img);
  if (G_flags->verbosity >= 2) {
    Printf("%s: %s\n", __FUNCTION__, rtn_name.c_str());
  }

  // main()
  INSERT_BEFORE_2("main", Before_main);
  INSERT_AFTER_0("main", After_main);

  // malloc/free
  MATCH_NAME_AND_REPLACE_RTN_1(malloc, void *, size_t);
  MATCH_NAME_AND_REPLACE_RTN_2(calloc, void *, size_t, size_t);
  MATCH_NAME_AND_REPLACE_RTN_1(free, void, void *);

  INSERT_BEFORE_2("mmap", Before_mmap);
  INSERT_AFTER_1("mmap", After_mmap);

  // ThreadSanitizerQuery
  WrapFunc4(img, rtn, "ThreadSanitizerQuery",
            (AFUNPTR)Wrap_ThreadSanitizerQuery);

  // pthread create/join
  WrapFunc4(img, rtn, "pthread_create", (AFUNPTR)Wrap_pthread_create);
  WrapFunc4(img, rtn, "pthread_join", (AFUNPTR)Wrap_pthread_join);

   // pthread_cond_*
  INSERT_BEFORE_1("pthread_cond_signal", Before_pthread_cond_signal);
  INSERT_BEFORE_2("pthread_cond_wait", Before_pthread_cond_wait);
  INSERT_AFTER_0("pthread_cond_wait", After_pthread_cond_wait);

  INSERT_BEFORE_2("pthread_cond_timedwait", Before_pthread_cond_wait);
  INSERT_AFTER_1("pthread_cond_timedwait", After_pthread_cond_timedwait);

  // pthread_mutex_*
  INSERT_BEFORE_1("pthread_mutex_init", Before_pthread_mutex_init);
  INSERT_BEFORE_1("pthread_mutex_destroy", Before_pthread_mutex_destroy);
  INSERT_BEFORE_1("pthread_mutex_unlock", Before_pthread_unlock);

  INSERT_BEFORE_1("pthread_mutex_lock", Before_pthread_lock);
  INSERT_BEFORE_1("pthread_mutex_trylock", Before_pthread_lock);

  INSERT_AFTER_1("pthread_mutex_lock", After_pthread_lock);
  INSERT_AFTER_1("pthread_mutex_trylock", After_pthread_trylock);


  // pthread_rwlock_*
  INSERT_BEFORE_1("pthread_rwlock_init", Before_pthread_rwlock_init);
  INSERT_BEFORE_1("pthread_rwlock_destroy", Before_pthread_rwlock_destroy);

  INSERT_BEFORE_1("pthread_rwlock_unlock", Before_pthread_unlock);

  INSERT_BEFORE_1("pthread_rwlock_wrlock", Before_pthread_lock);
  INSERT_AFTER_1 ("pthread_rwlock_wrlock", After_pthread_lock);

  INSERT_BEFORE_1("pthread_rwlock_rdlock", Before_pthread_lock);
  INSERT_AFTER_1 ("pthread_rwlock_rdlock", After_pthread_rdlock);

  INSERT_BEFORE_1("pthread_rwlock_trywrlock", Before_pthread_lock);
  INSERT_AFTER_1 ("pthread_rwlock_trywrlock", After_pthread_trylock);

  INSERT_BEFORE_1("pthread_rwlock_tryrdlock", Before_pthread_lock);
  INSERT_AFTER_1 ("pthread_rwlock_tryrdlock", After_pthread_tryrdlock);

  // pthread_spin_*
  INSERT_BEFORE_1("pthread_spin_init", Before_pthread_spin_init);
  INSERT_BEFORE_1("pthread_spin_destroy", Before_pthread_spin_destroy);
  INSERT_BEFORE_1("pthread_spin_unlock", Before_pthread_spin_unlock);

  INSERT_BEFORE_1("pthread_spin_lock", Before_pthread_lock);
  INSERT_BEFORE_1("pthread_spin_trylock", Before_pthread_lock);

  INSERT_AFTER_1("pthread_spin_lock", After_pthread_lock);
  INSERT_AFTER_1("pthread_spin_trylock", After_pthread_trylock);


  // pthread_barrier_*
  INSERT_BEFORE_1("pthread_barrier_wait", Before_pthread_barrier_wait);
  INSERT_AFTER_0("pthread_barrier_wait", After_pthread_barrier_wait);

  // sem_*
  INSERT_AFTER_1("sem_open", After_sem_open);
  INSERT_BEFORE_1("sem_post", Before_sem_post);
  INSERT_BEFORE_1("sem_wait", Before_sem_wait);
  INSERT_AFTER_0("sem_wait", After_sem_wait);
  INSERT_BEFORE_1("sem_trywait", Before_sem_wait);
  INSERT_AFTER_1("sem_trywait", After_sem_trywait);


#ifdef _MSC_VER
  INSERT_BEFORE_6("CreateThread", Before_CreateThread);
  INSERT_AFTER_1("CreateThread", After_CreateThread);

  WrapStdCallFunc1(rtn, "RtlInitializeCriticalSection",
                             (AFUNPTR)(Wrap_RtlInitializeCriticalSection));
  WrapStdCallFunc1(rtn, "RtlDeleteCriticalSection",
                             (AFUNPTR)(Wrap_RtlDeleteCriticalSection));
  WrapStdCallFunc1(rtn, "RtlEnterCriticalSection",
                             (AFUNPTR)(Wrap_RtlEnterCriticalSection));
  WrapStdCallFunc1(rtn, "RtlTryEnterCriticalSection",
                             (AFUNPTR)(Wrap_RtlTryEnterCriticalSection));
  WrapStdCallFunc1(rtn, "RtlLeaveCriticalSection",
                             (AFUNPTR)(Wrap_RtlLeaveCriticalSection));
  WrapStdCallFunc1(rtn, "SetEvent", (AFUNPTR)(Wrap_SetEvent));
  WrapStdCallFunc2(rtn, "WaitForSingleObject", (AFUNPTR)(Wrap_WaitForSingleObject));
  WrapStdCallFunc4(rtn, "VirtualAlloc", (AFUNPTR)(Wrap_VirtualAlloc));
  WrapStdCallFunc6(rtn, "ZwAllocateVirtualMemory", (AFUNPTR)(Wrap_ZwAllocateVirtualMemory));
  WrapStdCallFunc3(rtn, "RtlAllocateHeap", (AFUNPTR) Wrap_AllocateHeap);
#endif

  // Annotations.
  INSERT_BEFORE_4("AnnotateBenignRace", On_AnnotateBenignRace);
  INSERT_BEFORE_4("AnnotateExpectRace", On_AnnotateExpectRace);
  INSERT_BEFORE_4("AnnotateTraceMemory", On_AnnotateTraceMemory);
  INSERT_BEFORE_2("AnnotateNoOp", On_AnnotateNoOp);

  INSERT_BEFORE_3("AnnotateCondVarWait", On_AnnotateCondVarWait);
  INSERT_BEFORE_3("AnnotateCondVarSignal", On_AnnotateCondVarSignal);
  INSERT_BEFORE_3("AnnotateCondVarSignalAll", On_AnnotateCondVarSignal);

  INSERT_BEFORE_0("AnnotateIgnoreReadsBegin", On_AnnotateIgnoreReadsBegin);
  INSERT_BEFORE_0("AnnotateIgnoreReadsEnd", On_AnnotateIgnoreReadsEnd);
  INSERT_BEFORE_0("AnnotateIgnoreWritesBegin", On_AnnotateIgnoreWritesBegin);
  INSERT_BEFORE_0("AnnotateIgnoreWritesEnd", On_AnnotateIgnoreWritesEnd);
  INSERT_BEFORE_4("AnnotatePublishMemoryRange", On_AnnotatePublishMemoryRange);
  INSERT_BEFORE_4("AnnotateUnpublishMemoryRange", On_AnnotateUnpublishMemoryRange);
  INSERT_BEFORE_3("AnnotateMutexIsUsedAsCondVar", On_AnnotateMutexIsUsedAsCondVar);

  INSERT_BEFORE_3("AnnotatePCQCreate", On_AnnotatePCQCreate);
  INSERT_BEFORE_3("AnnotatePCQDestroy", On_AnnotatePCQDestroy);
  INSERT_BEFORE_3("AnnotatePCQPut", On_AnnotatePCQPut);
  INSERT_BEFORE_3("AnnotatePCQGet", On_AnnotatePCQGet);

  // I/O
  // TODO(kcc): add more I/O
  INSERT_BEFORE_0("write", Before_SignallingIOCall);
  INSERT_BEFORE_0("unlink", Before_SignallingIOCall);
  INSERT_BEFORE_0("rmdir", Before_SignallingIOCall);
//  INSERT_BEFORE_0("send", Before_SignallingIOCall);
  INSERT_AFTER_0("__read_nocancel", After_WaitingIOCall);
  INSERT_AFTER_0("fopen", After_WaitingIOCall);
  INSERT_AFTER_0("__fopen_internal", After_WaitingIOCall);
  INSERT_AFTER_0("open", After_WaitingIOCall);
  INSERT_AFTER_0("opendir", After_WaitingIOCall);
//  INSERT_AFTER_0("recv", After_WaitingIOCall);

  // strlen and friends.
  ReplaceFunc3(img, rtn, "memchr", (AFUNPTR)Replace_memchr);
  ReplaceFunc3(img, rtn, "strchr", (AFUNPTR)Replace_strchr);
  ReplaceFunc3(img, rtn, "index", (AFUNPTR)Replace_strchr);
  ReplaceFunc3(img, rtn, "strlen", (AFUNPTR)Replace_strlen);

  // pthread_once
  INSERT_BEFORE_1("pthread_once", Before_pthread_once);
  INSERT_AFTER_0("pthread_once", After_pthread_once);

  // __cxa_guard_acquire / __cxa_guard_release
  INSERT_BEFORE_1("__cxa_guard_acquire", Before_cxa_guard_acquire);
  INSERT_AFTER_1("__cxa_guard_acquire", After_cxa_guard_acquire);
  INSERT_BEFORE_1("__cxa_guard_release", Before_cxa_guard_release);
  INSERT_AFTER_0("__cxa_guard_release", After_cxa_guard_release);

  INSERT_BEFORE_0("atexit", On_atexit);
  INSERT_BEFORE_0("exit", On_exit);
}

// Pin calls this function every time a new img is loaded.
static void CallbackForIMG(IMG img, void *v) {
  if (G_flags->verbosity >= 2) {
    Printf("Started CallbackForIMG %s\n", IMG_Name(img).c_str());
  }
//  for(SYM sym = IMG_RegsymHead(img); SYM_Valid(sym); sym = SYM_Next(sym)) {
//    string name = SYM_Name(sym);
//    Printf("Sym: %s\n", name.c_str());
//  }

  // Check if we want to spend time searching this img for
  // some particular functions.
  string img_name = IMG_Name(img);
  // save the addresses of *all* routines in a map.
  if (!routine_address_map) {
    routine_address_map  = new map<uintptr_t, Routine*>;
  }
  for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec)) {
    for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn)) {
      string rtn_name = RTN_Name(rtn);
      Routine *routine = new Routine;
      routine->rtn_name = rtn_name;
      (*routine_address_map)[RTN_Address(rtn)] = routine;
      MaybeInstrumentOneRoutine(img, rtn);
    }
  }
}

//--------- Fini ---------- {{{1
static void CallbackForFini(INT32 code, void *v) {
//  Printf("# %s\n", __FUNCTION__);
//  Printf("#** dyn read/write: %'lld %'lld\n", dyn_read_count, dyn_write_count);
//  Printf("#** n_created_threads: %d\n", n_created_threads);

  DumpEvent(THR_END, 0, 0, 0, 0);
  ThreadSanitizerFini();
  if (G_flags->error_exitcode && GetNumberOfFoundErrors() > 0) {
    exit(G_flags->error_exitcode);
  }
}

void CallbackForDetach(VOID *v) {
  CHECK(g_attached_to_running_process);
  Printf("ThreadSanitizerPin: detached\n");
}

//--------- Main -------------------------- {{{1
int main(INT32 argc, CHAR **argv) {
  PIN_Init(argc, argv);
  PIN_InitSymbols();

  G_out = stderr;

  // Init ThreadSanitizer.
  G_flags = new FLAGS;
  int first_param = 1;
  // skip until '-t something.so'.
  for (; first_param < argc && argv[first_param] != string("-t");
       first_param++) {
    if (argv[first_param] == string("-pid")) {
      g_attached_to_running_process = true;
      Printf("INFO: ThreadSanitizerPin; attached mode\n");
    }
  }
  first_param += 2;
  vector<string> args;
  for (; first_param < argc; first_param++) {
    string param = argv[first_param];
    if (param == "--") break;
    if (param == "-short_name") continue;
    if (param == "1") continue;
    args.push_back(param);
  }
  ThreadSanitizerParseFlags(&args);
  ThreadSanitizerInit();

  // Set up PIN callbacks.
  PIN_AddThreadStartFunction(CallbackForThreadStart, 0);
  //PIN_AddThreadFiniFunction(CallbackForThreadFini, 0);
  PIN_AddFiniFunction(CallbackForFini, 0);
  IMG_AddInstrumentFunction(CallbackForIMG, 0);
  TRACE_AddInstrumentFunction(CallbackForTRACE, 0);
  //  PIN_AddDetachFunction(CallbackForDetach, 0);

  Report("ThreadSanitizerPin: "
         "pure-happens-before=%s fast-mode=%s ignore-in-dtor=%s\n",
         G_flags->pure_happens_before ? "yes" : "no",
         G_flags->fast_mode ? "yes" : "no",
         G_flags->ignore_in_dtor ? "yes" : "no");
  if (DEBUG_MODE) {
    Report("INFO: Debug build\n");
  }
  // Fire!
  PIN_StartProgram();
  return 0;
}

//--------- Include thread_sanitizer.cc --------- {{{1
// ... for performance reasons...
#ifdef INCLUDE_THREAD_SANITIZER_CC
# undef INCLUDE_THREAD_SANITIZER_CC
# include "thread_sanitizer.cc"
#endif

//--------- Questions about PIN -------------------------- {{{1
/* Questions about PIN:

  - Names (e.g. pthread_create@... __pthread_mutex_unlock)
  - How to get name of a global var by it's address?
  - How to get stack pointer at thread creation?
  - How to get a stack trace (other than intercepting calls, entries, exits)
  - assert with full stack trace?
  */
// end. {{{1
// vim:shiftwidth=2:softtabstop=2:expandtab
