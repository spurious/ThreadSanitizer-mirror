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

#include <cxxabi.h>  // __cxa_demangle

#include "thread_sanitizer.h"

#ifdef NDEBUG
# error "Please don't define NDEBUG"
#endif
#define CHECK assert

//--------- Simple SpinLock ------------------ {{{1
// Just a simple lock.
class TSLock {
 public:
  TSLock() : lock_(0) {}
  void Lock() {
    for (int i = -5; !TryLock(); i++) {
      if (i > 0)
        usleep(i * 10);
    }
  }
  void Unlock() {
    __sync_bool_compare_and_swap(&lock_, 1, 0);
  }
  bool TryLock() {
    if ( __sync_bool_compare_and_swap(&lock_, 0, 1)) {
      return true;
    }
    return false;
  }
 private:
  int32_t lock_;
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

static uintptr_t g_current_pc;

static bool g_attached_to_running_process = false;

//--------------- PinThread ----------------- {{{1
struct PinThread {
  OS_THREAD_ID os_tid;
  THREADID     last_child_tid;
  THREADID     parent_tid;
  size_t       last_malloc_size;
  size_t       last_mmap_size;
  pthread_t   *child_ptid_ptr;
  pthread_t    my_ptid;
  pthread_t    joined_ptid;
  bool         started;
  uintptr_t    cxa_guard;
  int          call_stack_depth;
};

// Array of pin threads, indexed by pin's THREADID.
static PinThread *g_pin_threads;

//------------- ThreadSanitizer exports ------------ {{{1
string Demangle(const char *str) {
  int status;
  char *demangled = __cxxabiv1::__cxa_demangle(str, 0, 0, &status);
  if (demangled) {
    string res = demangled;
    free(demangled);
    return res;
  }
  return str;
}

void PcToStrings(uintptr_t pc, bool demangle,
                string *img_name, string *rtn_name,
                string *file_name, int *line_no) {
  if (G_flags->symbolize) {
    RTN rtn;
    {
      // ClientLock must be held.
      ScopedReentrantClientLock lock(__LINE__);
      PIN_GetSourceLocation(pc, NULL, line_no, file_name);
      rtn = RTN_FindByAddress(pc);
    }
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

//--------- Instrumentation callbacks --------------- {{{1
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
//
static void Before_cxa_guard_acquire(THREADID tid, ADDRINT pc, ADDRINT guard) {
//  Printf("T%d A+ %lx\n", tid, guard);
//  g_pin_threads[tid].cxa_guard = guard;
//  IgnoreAllBegin(tid, pc);
//  cxa_guards.insert(guard);
}

static void After_cxa_guard_acquire(THREADID tid, ADDRINT pc) {
//  ADDRINT guard = g_pin_threads[tid].cxa_guard;
//  Printf("T%d A- %lx\n", tid, guard);
//  IgnoreAllEnd(tid, pc);
//  DumpEvent(WAIT_BEFORE, tid, pc, guard, 0);
//  DumpEvent(WAIT_AFTER, tid, pc, 0, 0);
}

static void Before_cxa_guard_release(THREADID tid, ADDRINT pc, ADDRINT guard) {
//  DumpEvent(SIGNAL, tid, pc, guard, 0);
//  IgnoreAllBegin(tid, pc);
//  g_pin_threads[tid].cxa_guard = guard;
//  Printf("T%d R+ %lx\n", tid, guard);
}

static void After_cxa_guard_release(THREADID tid, ADDRINT pc) {
//  ADDRINT guard = g_pin_threads[tid].cxa_guard;
//  Printf("T%d R- %lx\n", tid, guard);
//  IgnoreAllEnd(tid, pc);
}

static void Before_pthread_once(THREADID tid, ADDRINT pc, ADDRINT control) {
//  Printf("T%d once %lx\n", tid, control);
  IgnoreAllBegin(tid, pc);
}
static void After_pthread_once(THREADID tid, ADDRINT pc) {
//  Printf("T%d once \n", tid);
  IgnoreAllEnd(tid, pc);
}

void TmpCallback1(THREADID tid, ADDRINT pc) {
  Printf("%s T%d %lx\n", __FUNCTION__, tid, pc);
}
void TmpCallback2(THREADID tid, ADDRINT pc) {
  Printf("%s T%d %lx\n", __FUNCTION__, tid, pc);
}

//--------- Threads --------------------------------- {{{2
static void Before_pthread_create(THREADID tid, ADDRINT pc,
                                  ADDRINT arg1, ADDRINT arg2,
                                  ADDRINT arg3, ADDRINT arg4) {
  n_created_threads++;
  g_pin_threads[tid].child_ptid_ptr = (pthread_t*)arg1;
  *(pthread_t*)arg1 = NULL;
  IgnoreAllBegin(tid, pc);
  // Printf("%s: T=%d %lx\n", __FUNCTION__, tid, arg1);
}

void CallbackForThreadStart(THREADID tid, CONTEXT *ctxt,
                            INT32 flags, void *v) {
  OS_THREAD_ID my_os_tid = PIN_GetTid();
  OS_THREAD_ID parent_os_tid = PIN_GetParentTid();

  if (g_pin_threads == NULL) {
    g_pin_threads = new PinThread[kMaxThreads];
  }

  bool has_parent = true;
  if (parent_os_tid == INVALID_OS_THREAD_ID) {
    // Main thread or we have attached to a runnign process.
    has_parent = false;
  } else {
    CHECK(tid > 0);
  }

  CHECK(tid < kMaxThreads);
  memset(&g_pin_threads[tid], 0, sizeof(PinThread));
  g_pin_threads[tid].os_tid = my_os_tid;

  THREADID parent_tid = -1;
  if (has_parent) {
    // Find out the parent's tid.
    for (parent_tid = tid - 1; parent_tid > 0; parent_tid--) {
      if (g_pin_threads[parent_tid].os_tid == parent_os_tid)
        break;
    }
    CHECK(parent_tid != (THREADID)-1);
    g_pin_threads[tid].parent_tid = parent_tid;
  }

//    Printf("#  tid=%d parent_tid=%d my_os_tid=%d parent_os_tid=%d\n",
//           tid, parent_tid, my_os_tid, parent_os_tid);

  g_pin_threads[tid].child_ptid_ptr = NULL;
  if (has_parent) {
    g_pin_threads[parent_tid].last_child_tid = tid;
  }
}

static void After_pthread_create(THREADID tid, ADDRINT pc, ADDRINT ret) {
  IgnoreAllEnd(tid, pc);
  // Spin, waiting for last_child_tid to appear (i.e. wait for the thread to
  // actually start) so that we know the child's tid. No locks.
  while (!__sync_add_and_fetch(&g_pin_threads[tid].last_child_tid, 0)) {
    usleep(0);
  }

  THREADID last_child_tid = g_pin_threads[tid].last_child_tid;
  pthread_t child_ptid = *(pthread_t*)g_pin_threads[tid].child_ptid_ptr;
  CHECK(last_child_tid);

  DumpEvent(THR_SET_PTID, last_child_tid, 0, child_ptid, 0);
  g_pin_threads[last_child_tid].my_ptid = child_ptid;
  g_pin_threads[tid].last_child_tid = 0;
}

static void Before_pthread_join(THREADID tid, ADDRINT pc,
                                ADDRINT arg1, ADDRINT arg2) {
  DumpEvent(THR_JOIN_BEFORE, tid, 0, arg1, 0);
  g_pin_threads[tid].joined_ptid = (pthread_t)arg1;
}

void CallbackForThreadFini(THREADID tid, const CONTEXT *ctxt,
                          INT32 code, void *v) {
  // We can not DumpEvent here,
  // due to possible deadlock with PIN's internal lock.
}

static void After_pthread_join(THREADID tid, ADDRINT pc, ADDRINT ret) {
  THREADID joined_tid = 0;
  for (joined_tid = 1; joined_tid < kMaxThreads; joined_tid++) {
    if (g_pin_threads[joined_tid].my_ptid == g_pin_threads[tid].joined_ptid)
      break;
  }
  CHECK(joined_tid < kMaxThreads);
  g_pin_threads[joined_tid].my_ptid = 0;
  DumpEvent(THR_END, joined_tid, 0, 0, 0);
  DumpEvent(THR_JOIN_AFTER, tid, 0, 0, 0);
}


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
static void Before_malloc(THREADID tid, ADDRINT pc, ADDRINT size) {
//  IgnoreAllBegin(tid, pc);
  g_pin_threads[tid].last_malloc_size = size;
}
static void Before_calloc(THREADID tid, ADDRINT pc, ADDRINT n, ADDRINT size) {
//  IgnoreAllBegin(tid, pc);
  g_pin_threads[tid].last_malloc_size = n * size;
}

static void After_malloc(THREADID tid, ADDRINT pc, ADDRINT ret) {
  size_t last_malloc_size = g_pin_threads[tid].last_malloc_size;
  g_pin_threads[tid].last_malloc_size = 0;
  DumpEvent(MALLOC, tid, pc, ret, last_malloc_size);
//  IgnoreAllEnd(tid, pc);
}
static void Before_free(THREADID tid, ADDRINT pc, ADDRINT ptr) {
//  IgnoreAllBegin(tid, pc);
  DumpEvent(FREE, tid, pc, ptr, 0);
}


void Before_mmap(THREADID tid, ADDRINT pc, ADDRINT start, ADDRINT len) {
  g_pin_threads[tid].last_mmap_size = len;
}
void After_mmap(THREADID tid, ADDRINT pc, ADDRINT ret) {
  if (ret != (ADDRINT)-1L) {
    size_t last_mmap_size = g_pin_threads[tid].last_mmap_size;
    g_pin_threads[tid].last_mmap_size = 0;
    DumpEvent(MALLOC, tid, pc, ret, last_mmap_size);
  }
}

//-------- Routines and stack ---------------------- {{{2


void InsertBeforeEvent_RoutineEntry(THREADID tid, ADDRINT pc,
                                    ADDRINT sp, Routine *routine) {
  DumpEvent(SBLOCK_ENTER, tid, pc, 0, 0);
}

static void InsertAfterEvent_RoutineExit(THREADID tid, ADDRINT pc, ADDRINT sp) {
  if (g_pin_threads[tid].call_stack_depth > 0) {
    // TODO(kcc): somehow on l32 we may get more exits than calls...
    DumpEvent(RTN_EXIT, tid, 0, 0, 0);
    g_pin_threads[tid].call_stack_depth--;
  }
}
void InsertBeforeEvent_Call(THREADID tid, ADDRINT pc, ADDRINT target, ADDRINT sp) {
  DumpEvent(RTN_CALL, tid, pc, target, 0);
  g_pin_threads[tid].call_stack_depth++;

}

static void InsertAfterEvent_SpUpdate(THREADID tid, ADDRINT pc, ADDRINT sp) {
}

void InsertBeforeEvent_SblockEntry(THREADID tid, ADDRINT pc) {
  DumpEvent(SBLOCK_ENTER, tid, pc, 0, 0);
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

  if (INS_IsRet(tail)) {
    INS_InsertCall(tail, IPOINT_BEFORE,
                   (AFUNPTR)InsertAfterEvent_RoutineExit,
                   IARG_THREAD_ID, IARG_INST_PTR,
                   IARG_REG_VALUE, REG_STACK_PTR,
                   IARG_END);
  }
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
                     IARG_END);
  }

  for(BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
    InstrumentBbl(bbl, rtn, ignore_memory);
  }
}

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

#define INSERT_FN_HELPER(point, name, rtn, to_insert, args...) \
    RTN_Open(rtn); \
    if (G_flags->verbosity >= 2) Printf("RTN: Inserting %-50s (%s) %s (%s) img: %s\n", \
    #to_insert, #point, RTN_Name(rtn).c_str(), name, IMG_Name(img).c_str());\
    RTN_InsertCall(rtn, point, (AFUNPTR)to_insert, IARG_THREAD_ID, \
                   IARG_INST_PTR, args, IARG_END);\
    RTN_Close(rtn); \

#define INSERT_FN(point, name, to_insert, args...) \
  while (RtnMatchesName(rtn_name, name)) {\
    INSERT_FN_HELPER(point, name, rtn, to_insert, args); \
    break;\
  }\


#define INSERT_BEFORE_FN(name, to_insert, args...) \
    INSERT_FN(IPOINT_BEFORE, name, to_insert, args)

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

#define INSERT_AFTER_FN(name, to_insert, args...) \
    INSERT_FN(IPOINT_AFTER, name, to_insert, args)

#define INSERT_AFTER_0(name, to_insert) \
    INSERT_AFTER_FN(name, to_insert, IARG_END)

#define INSERT_AFTER_1(name, to_insert) \
    INSERT_AFTER_FN(name, to_insert, IARG_FUNCRET_EXITPOINT_VALUE)


#define INSERT_FN_SLOW(point, name, to_insert, args...) \
  while (RTN_Valid((rtn = RTN_FindByName(img, name)))) { \
    INSERT_FN_HELPER(point, name, rtn, to_insert, args); \
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

static void MaybeInstrumentOneRoutine(IMG img, RTN rtn) {
  if (IgnoreImage(img)) {
    return;
  }
  string rtn_name = RTN_Name(rtn);
  if (G_flags->verbosity >= 2) {
    Printf("%s: %s\n", __FUNCTION__, rtn_name.c_str());
  }

  // main()
  INSERT_BEFORE_2("main", Before_main);
  INSERT_AFTER_0("main", After_main);

  // malloc/free
  INSERT_BEFORE_1("malloc", Before_malloc);
  INSERT_AFTER_1("malloc", After_malloc);

  INSERT_BEFORE_2("calloc", Before_calloc);
  INSERT_AFTER_1("calloc", After_malloc);


  INSERT_BEFORE_1("free", Before_free);
//  INSERT_AFTER_0("free", IgnoreAllEnd);


  INSERT_BEFORE_2("mmap", Before_mmap);
  INSERT_AFTER_1("mmap", After_mmap);

  // pthread create/join
  INSERT_BEFORE_4("pthread_create", Before_pthread_create);
  INSERT_AFTER_1("pthread_create", After_pthread_create);
  INSERT_BEFORE_2("pthread_join", Before_pthread_join);
  INSERT_AFTER_1("pthread_join", After_pthread_join);

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
  // TODO(kcc): do something smarter here.
  INSERT_BEFORE_0("strlen", IgnoreAllBegin);
  INSERT_AFTER_0("strlen", IgnoreAllEnd);
  INSERT_BEFORE_0("index", IgnoreAllBegin);
  INSERT_AFTER_0("index", IgnoreAllEnd);

  // pthread_once
  INSERT_BEFORE_1("pthread_once", Before_pthread_once);
  INSERT_AFTER_0("pthread_once", After_pthread_once);

  // __cxa_guard_acquire / __cxa_guard_release
  // TODO(kcc): uncomment this (and make it work on test108,test114).
  INSERT_BEFORE_1("__cxa_guard_acquire", Before_cxa_guard_acquire);
  INSERT_AFTER_0("__cxa_guard_acquire", After_cxa_guard_acquire);
  INSERT_BEFORE_1("__cxa_guard_release", Before_cxa_guard_release);
  INSERT_AFTER_0("__cxa_guard_release", After_cxa_guard_release);

  INSERT_BEFORE_0("atexit", On_atexit);
  INSERT_BEFORE_0("exit", On_exit);
}

// Pin calls this function every time a new img is loaded.
static void CallbackForIMG(IMG img, void *v)
{

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
  Printf("# %s\n", __FUNCTION__);
  Printf("#** dyn read/write: %'lld %'lld\n", dyn_read_count, dyn_write_count);
  Printf("#** n_created_threads: %d\n", n_created_threads);

  DumpEvent(THR_END, 0, 0, 0, 0);
  ThreadSanitizerFini();
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
  PIN_AddThreadFiniFunction(CallbackForThreadFini, 0);
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
#else

#endif


//--------- Questions about PIN -------------------------- {{{1
/* Questions about PIN:
  
  - Am I allowed to call pthread_create() in the pin tool?  -- **NO**
  - How to Instrument thread create/join events in parent 
  (other than intercepting pthread_create/join*)
  - Names (e.g. pthread_create@... __pthread_mutex_unlock)
  - How to get name of a global var by it's address?
  - How to get stack pointer at thread creation? 
  - How to get a stack trace (other than intercepting calls, entries, exits) 
  - assert with full stack trace?
  */
// end. {{{1
// vim:shiftwidth=2:softtabstop=2:expandtab
