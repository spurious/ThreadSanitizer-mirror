#include "tsan_rtl.h"

#include "ts_trace_info.h"

#include "ts_literace.h"
#include "ts_lock.h"

#include <elf.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define EXTRA_REPLACE_PARAMS tid_t tid, pc_t pc,
#define REPORT_READ_RANGE(x, size) do { \
    if (size) SPut(READ, tid, pc, (uintptr_t)(x), (size)); } while(0)
#define REPORT_WRITE_RANGE(x, size) do { \
    if (size) SPut(WRITE, tid, pc, (uintptr_t)(x), (size)); } while(0)
#include "ts_replace.h"

#ifdef DEBUG_LEVEL
#define DEBUG 1
#endif

#if (DEBUG_LEVEL == 2)
# define DDPrintf(params...) \
    Printf(params)
#else
# define DDPrintf(params...)
#endif

#ifdef DEBUG
# define DPrintf(params...) \
    Printf(params)
# define DEBUG_DO(code) \
  do { code } while(0)
#else
# define DPrintf(params...)
# define DEBUG_DO(code)
#endif

extern bool global_ignore;
static const size_t kTLEBSize = 2000;

struct ThreadInfo {
  tid_t tid;
  uintptr_t TLEB[kTLEBSize];
  TraceInfoPOD *trace_info;
};

struct LLVMDebugInfo {
  LLVMDebugInfo()
    : pc(0), line(0) { }
  pc_t pc;
  string symbol;
  string file;
  string path;
  string fullpath;  // = path + "/" + file
  uintptr_t line;
};

map<pc_t, LLVMDebugInfo> *debug_info = NULL;

void SplitString(string &src, char delim, vector<string> *dest,
                 bool want_empty) {
  string curr_word;
  for (size_t i = 0; i < src.size(); ++i) {
    if (src[i] == delim) {
      if ((curr_word != "") || want_empty) dest->push_back(curr_word);
      curr_word = "";
    } else {
      curr_word += src[i];
    }
  }
  if ((curr_word != "") || want_empty) dest->push_back(curr_word);
}

__thread ThreadInfo INFO;
__thread int INIT = 0;
// A condvar passed to child thread to notify the parent that the TID is
// already set up.
__thread pthread_cond_t *ParentCond;
__thread int events = 0;
typedef void (tsd_destructor)(void*);
struct tsd_slot {
  pthread_key_t key;
  tsd_destructor *dtor;
};

// TODO(glider): PTHREAD_KEYS_MAX
// TODO(glider): there could be races if pthread_key_create is called from
// concurrent threads. This is prohibited by POSIX, however.
tsd_slot tsd_slots[100];
int tsd_slot_index = -1;

int RTL_INIT = 0;
int PTH_INIT = 0;
int DBG_INIT = 0;
int HAVE_THREAD_0 = 0;
int32_t bb_unique_id = 0;


std::map<pthread_t, tid_t> Tids;
std::map<tid_t, pthread_t> PThreads;
std::map<tid_t, bool> Finished;
// TODO(glider): verify that we need this much condvars.
std::map<tid_t, pthread_cond_t*> ChildThreadStartConds;
std::map<tid_t, pthread_cond_t*> FinishConds; // TODO(glider): we shouldn't need these.
std::map<pthread_t, pthread_cond_t*> InitConds; // TODO(glider): we shouldn't need these.
tid_t max_tid = 0;

__thread  sigset_t glob_sig_blocked, glob_sig_old;

// We don't initialize these.
struct sigaction signal_actions[NSIG];  // protected by GIL
__thread siginfo_t pending_signals[NSIG];
__thread bool pending_signal_flags[NSIG];
__thread bool have_pending_signals;

// Stats {{{1
#undef ENABLE_STATS
#ifdef ENABLE_STATS
int stats_lock_taken = 0;
int stats_events_processed = 0;
int stats_cur_events = 0;
int stats_non_local = 0;
const int kNumBuckets = 11;
int stats_event_buckets[kNumBuckets];
#endif
// }}}


pthread_mutex_t debug_info_lock = PTHREAD_MUTEX_INITIALIZER;

class DbgInfoLock {
 public:
  DbgInfoLock() {
    __real_pthread_mutex_lock(&debug_info_lock);
  }
  ~DbgInfoLock() {
    __real_pthread_mutex_unlock(&debug_info_lock);
  }
};

// BLOCK_SIGNALS macro enables blocking the signals any time the global lock
// is taken. This brings huge overhead to the lock and looks unnecessary now, 
// because our signal handler can run even under the global lock.
//#define BLOCK_SIGNALS 1
#undef BLOCK_SIGNALS

pthread_mutex_t global_lock = PTHREAD_MUTEX_INITIALIZER;
#define GIL_LOCK __real_pthread_mutex_lock
#define GIL_UNLOCK __real_pthread_mutex_unlock
#define GIL_TRYLOCK __real_pthread_mutex_trylock

pthread_t gil_owner = 0;
__thread int gil_depth = 0;

// Reentrancy counter {{{1
__thread int IN_RTL = 0;

#ifdef DEBUG
#define CHECK_IN_RTL() do { \
  assert((IN_RTL >= 0) && (IN_RTL <= 5)); \
} while (0)
#else
#define CHECK_IN_RTL()
#endif
// }}}


void GIL::Lock() {
#if BLOCK_SIGNALS
  sigfillset(&glob_sig_blocked);
  pthread_sigmask(SIG_BLOCK, &glob_sig_blocked, &glob_sig_old);
#endif
  if (!gil_depth) {
    GIL_LOCK(&global_lock);
#ifdef ENABLE_STATS
    stats_lock_taken++;
#endif
    IN_RTL++;
    CHECK_IN_RTL();
  }
#ifdef DEBUG
  gil_owner = pthread_self();
#endif
  gil_depth++;
}

bool GIL::TryLock() {
#ifdef BLOCK_SIGNALS
  sigfillset(&glob_sig_blocked);
  pthread_sigmask(SIG_BLOCK, &glob_sig_blocked, &glob_sig_old);
#endif
#ifdef DEBUG
  gil_owner = pthread_self();
#endif
  bool result;
  if (!gil_depth) {
    result = !(bool) GIL_TRYLOCK(&global_lock);
    if (result) {
      gil_depth++;
      IN_RTL++;
      CHECK_IN_RTL();
    }
    return result;
  } else {
    return false;
  }
}

void GIL::Unlock() {
  if (gil_depth == 1) {
    // TODO(glider): don't need to handle pending signals here.
    ///unsafe_clear_pending_signals();
    gil_depth--;
#ifdef DEBUG
    gil_owner = 0;
#endif
#ifdef ENABLE_STATS
    if (UNLIKELY(G_flags->verbosity)) {
      if (stats_cur_events<kNumBuckets) {
        stats_event_buckets[stats_cur_events]++;
      }
      stats_cur_events = 0;
    }
#endif
    GIL_UNLOCK(&global_lock);
    IN_RTL--;
    CHECK_IN_RTL();
  } else {
    gil_depth--;
  }
#ifdef BLOCK_SIGNALS
  pthread_sigmask(SIG_SETMASK, &glob_sig_old, &glob_sig_old);
  siginfo_t info;
  struct timespec zero_timeout = {0, 0};
  int sig = sigtimedwait(&glob_sig_old, &info, &zero_timeout);
  if (sig > 0) {
    // TODO(glider): signal_actions should be accessed under the global lock.
    signal_actions[sig].sa_sigaction(sig, &info, NULL);
  }
#endif
}
#ifdef DEBUG
int GIL::GetDepth() {
  return gil_depth;
}
#endif

bool isThreadLocalEvent(EventType type) {
  switch (type) {
    case READ:
    case WRITE:
    case SBLOCK_ENTER:
    case RTN_CALL:
    case RTN_EXIT:
      return true;
    case IGNORE_WRITES_END:
    case IGNORE_READS_END:
    case IGNORE_WRITES_BEG:
    case IGNORE_READS_BEG:
      return false;
    default:
      return false;
  }
}

extern void ExSPut(EventType type, tid_t tid, pc_t pc,
                   uintptr_t a, uintptr_t info) {
  SPut(type, tid, pc, a, info);
}
extern void ExRPut(EventType type, tid_t tid, pc_t pc,
                   uintptr_t a, uintptr_t info) {
  RPut(type, tid, pc, a, info);
}
extern void ExPut(EventType type, tid_t tid, pc_t pc,
                   uintptr_t a, uintptr_t info) {
  Put(type, tid, pc, a, info);
}

inline void SPut(EventType type, tid_t tid, pc_t pc,
                 uintptr_t a, uintptr_t info) {
#ifdef DEBUG
  assert(HAVE_THREAD_0 || ((type == THR_START) && (tid == 0)));
  assert(RTL_INIT == 1);
#endif

  if (type != THR_START) flush_tleb();
  if (!G_flags->dry_run) {
    Event event(type, tid, pc, a, info);
    if (G_flags->verbosity) {
      if ((G_flags->verbosity >= 2) ||
          (type == THR_START) || (type == THR_END) || (type == THR_JOIN_AFTER) || (type == THR_CREATE_BEFORE)) {
        IN_RTL++;
        CHECK_IN_RTL();
        event.Print();
        IN_RTL--;
        CHECK_IN_RTL();
      }
    }
#ifdef ENABLE_STATS
    if (G_flags->verbosity) {
      stats_events_processed++;
      stats_cur_events++;
      stats_non_local++;
    }
#endif

    IN_RTL++;
    CHECK_IN_RTL();
    {
      ThreadSanitizerHandleOneEvent(&event);
    }
    IN_RTL--;
    CHECK_IN_RTL();
    unsafe_clear_pending_signals();
  }
  if ((type==THR_START) && (tid==0)) HAVE_THREAD_0 = 1;
}

void inline flush_trace() {
#ifdef DEBUG
  // TODO(glider): PutTrace shouldn't be called without a lock taken.
  // However flushing events from unsafe_clear_pending_signals (called from
  // GIL::Unlock) is done under a mutex.
//  assert(!GIL::GetDepth());
#endif
#ifdef DEBUG
  assert(HAVE_THREAD_0 || ((type == THR_START) && (tid == 0)));
  assert(RTL_INIT == 1);
#endif
  if (!global_ignore && LIKELY(!G_flags->dry_run)) {
    tid_t tid = INFO.tid;
    TraceInfoPOD *trace = INFO.trace_info;
    uintptr_t *tleb = INFO.TLEB;
#ifdef ENABLE_STATS
    stats_events_processed += trace->n_mops_;
    stats_cur_events += trace->n_mops_;
#endif

    // If the trace is encountered for the first time, set its ID to a unique
    // number.
    // TODO(glider): we've got a benign race here: if a basic block is
    // simultaneously executed on multiple threads its ID may change several
    // times. As a result the count of this block executions may be off by the
    // number of threads.
    // TODO(glider): no need to set trace->id
    // if G_flags->literace_sampling == 0.
    if (!trace->id_) {
      bb_unique_id = NoBarrier_AtomicIncrement(&bb_unique_id);
      ANNOTATE_BENIGN_RACE(&trace->id_, "Race on trace->id");
      trace->id_ = bb_unique_id;
    }

    // Increment the trace counter in a racey way. This can lead to small
    // deviations if the trace is hot, but we can afford them.
    // Unfortunately this also leads to cache ping-pong and may affect the
    // performance.
    if (UNLIKELY(G_flags->show_stats)) trace->counter_++;

    if (LIKELY(G_flags->literace_sampling == 0) ||
        !LiteRaceSkipTrace(tid, trace->id_, G_flags->literace_sampling)) {
      IN_RTL++;
      CHECK_IN_RTL();
      assert(trace);
      if (UNLIKELY(G_flags->verbosity >= 2)) {
        Event sblock(SBLOCK_ENTER, tid, trace->pc_, 0, trace->n_mops_);
        sblock.Print();
        assert(trace->n_mops_);
        for (size_t i = 0; i < trace->n_mops_; i++) {
          if (trace->mops_[i].is_write) {
            Event event(WRITE, tid, trace->mops_[i].pc, tleb[i], trace->mops_[i].size);
            event.Print();
          } else {
            Event event(READ, tid, trace->mops_[i].pc, tleb[i], trace->mops_[i].size);
            event.Print();
          }
        }
      }
      {
        ThreadSanitizerHandleTrace(tid, reinterpret_cast<TraceInfo*>(trace), tleb);
      }

      // TODO(glider): the instrumentation pass may generate basic blocks that
      // are larger than sizeof(TLEB). There should be a flag to control this,
      // because we don't want to check the trace size in runtime.
      DCHECK(trace->n_mops_ <= kTLEBSize);
      // Clean up the TLEB. It's better to do this in ThreadSanitizer, however.
      // TODO(glider): add a DCHECK() for this once it's done.
      for (size_t i = 0; i < trace->n_mops_; i++) tleb[i] = 0;
      IN_RTL--;
      CHECK_IN_RTL();
      unsafe_clear_pending_signals();
    }
  }
}

inline void Put(EventType type, tid_t tid, pc_t pc,
                uintptr_t a, uintptr_t info) {
#ifdef DEBUG
  assert(!isThreadLocalEvent(type));
#endif
  SPut(type, tid, pc, a, info);
}

// RPut is strictly for putting RTN_CALL and RTN_EXIT events.
inline void RPut(EventType type, tid_t tid, pc_t pc,
                 uintptr_t a, uintptr_t info) {
#ifdef DEBUG
  assert(HAVE_THREAD_0 || ((type == THR_START) && (tid == 0)));
  assert(RTL_INIT == 1);
#endif
  if (LIKELY(!G_flags->dry_run)) {
    Event event(type, tid, pc, a, info);
    if (UNLIKELY(G_flags->verbosity >= 2)) {
      event.Print();
    }
#ifdef ENABLE_STATS
    if (UNLIKELY(G_flags->verbosity)) {
      stats_events_processed++;
      stats_cur_events++;
    }
    if (!isThreadLocalEvent(type)) stats_non_local++;
#endif
    // Do not flush writes to 0x0.
    IN_RTL++;
    CHECK_IN_RTL();
    if (type == RTN_CALL) {
      ThreadSanitizerHandleRtnCall(tid, pc, a, IGNORE_BELOW_RTN_UNKNOWN);
    } else {
      ThreadSanitizerHandleRtnExit(tid);
    }
    IN_RTL--;
    CHECK_IN_RTL();
    unsafe_clear_pending_signals();
  }
}

void finalize() {
  IN_RTL++;
  CHECK_IN_RTL();
  // atexit hooks are ran from a single thread.
  ThreadSanitizerFini();
  IN_RTL--;
  CHECK_IN_RTL();
#if ENABLE_STATS
  Printf("Locks: %d\nEvents: %d\n",
         stats_lock_taken, stats_events_processed);
  Printf("Non-bufferable events: %d\n", stats_non_local);
  int total_events = 0;
  int total_locks = 0;
  for (int i=0; i<kNumBuckets; i++) {
    Printf("%d events under a lock: %d times\n", i, stats_event_buckets[i]);
    total_locks += stats_event_buckets[i];
    total_events += stats_event_buckets[i]*i;
  }
  Printf("Locks within buckets: %d\n", total_locks);
  Printf("Events within buckets: %d\n", total_events);
#endif
  if (G_flags->error_exitcode && GetNumberOfFoundErrors() > 0) {
    // This is the last atexit hook, so it's ok to terminate the program.
    _exit(G_flags->error_exitcode);
  }
}

inline void init_debug() {
  assert(DBG_INIT == 0);
  char *dbg_info = getenv("TSAN_DBG_INFO");
  if (dbg_info) {
    ReadDbgInfo(dbg_info);
  } else {
    ReadElf();
  }
  DBG_INIT = 1;
}

bool in_initialize = false;

bool initialize() {
  if (in_initialize) return false;
  if (RTL_INIT == 1) return true;
  in_initialize = true;

  // TODO(glider): do we need it?
  //assert(IN_RTL == 0);
  IN_RTL++;
  CHECK_IN_RTL();
  // Only one thread exists at this moment.
  G_flags = new FLAGS;
  G_out = stderr;
  vector<string> args;
  char *env = getenv("TSAN_ARGS");
  if (env) {
    string env_args(const_cast<char*>(env));
    size_t start = env_args.find_first_not_of(" ");
    size_t stop = env_args.find_first_of(" ", start);
    while(start != string::npos || stop != string::npos) {
      args.push_back(env_args.substr(start, stop - start));
      start = env_args.find_first_not_of(" ", stop);
      stop = env_args.find_first_of(" ", start);
    }
  }
#ifdef ENABLE_STATS
  for (int i=0; i<kNumBuckets; i++) {
    stats_event_buckets[i] = 0;
  }
#endif

  ThreadSanitizerParseFlags(&args);
  ThreadSanitizerInit();
  init_debug();
  IN_RTL--;
  CHECK_IN_RTL();
  global_ignore = false;
  __real_atexit(finalize);
  ANNOTATE_BENIGN_RACE(&bb_unique_id, "Race on bb_unique_id");
  RTL_INIT = 1;
  in_initialize = false;
  SPut(THR_START, 0, 0, 0, 0);

  return true;
}

// TODO(glider): we shouldn't need static initialization of the RTL:
//  -- it's fragile
//  -- we do initialize everything once GetTid is called for the first time.
#if 0
class Init {
 public:
  Init() {
    initialize();
  }
};

Init dummy_init;
#endif

// TODO(glider): GetPc should return valid PCs.
inline pc_t GetPc() {
  return 0;
}

extern pc_t ExGetPc() {
  return GetPc();
}

inline tid_t GetTid() {
  IN_RTL++;
  CHECK_IN_RTL();
  if (INIT == 0) {
    GIL scoped;
    // thread initialization
    pthread_t pt = pthread_self();
    INFO.tid = max_tid;
    max_tid++;
    INFO.trace_info = NULL;
    switch (RTL_INIT) {
      case 1: {
        Tids[pt] = INFO.tid;
        if (InitConds.find(pt) != InitConds.end()) {
          __real_pthread_cond_signal(InitConds[pt]);
        }
        DDPrintf("T%d: pthread_self()=%p\n", INFO.tid, (void*)pt);
        PThreads[INFO.tid] = pt;
        break;
              }
      case 0: {
        RTL_INIT = 2;
        if (!initialize()) return 0;
        RTL_INIT = 1;
        break;
              }
      case 2:
      default: {
                 // Printf() may call malloc() -- use __real_malloc().
                 IN_RTL++;
                 CHECK_IN_RTL();
                 Printf("ThreadSanitizer initialization reentrancy detected\n");
                 IN_RTL--;
                 CHECK_IN_RTL();
                 __real_exit(2);
               }
    }
    INIT = 1;
  }
  IN_RTL--;
  CHECK_IN_RTL();
  return INFO.tid;
}

extern tid_t ExGetTid() {
  return GetTid();
}


// Flushes the local TLEB assuming someone is holding the global lock already.
// Our RTL shouldn't need a thread to flush someone else's TLEB.
void inline flush_tleb() {
#ifdef DEBUG
  if (UNLIKELY(G_flags->verbosity >= 2)) {
    DDPrintf("flush_tleb\n");
  }
#endif
  ThreadInfo *info = &INFO;
  if (info) {
    if (info->trace_info) {
      flush_trace();
      info->trace_info = NULL;
    }
  } else {
    DPrintf("ERROR: thread %d not found!\n", GetTid());
    assert(0);
  }
}

typedef void *(pthread_worker)(void*);

struct callback_arg {
  pthread_worker *routine;
  void *arg;
  tid_t parent;
  pthread_attr_t *attr;
};

// TODO(glider): we should get rid of Finished[],
// as FinishConds should guarantee that the thread has finished.
void dump_finished() {
  map<tid_t, bool>::iterator iter;
  for (iter = Finished.begin(); iter != Finished.end(); ++iter) {
    DDPrintf("Finished[%d] = %d\n", iter->first, iter->second);
  }
}

void *pthread_callback(void *arg) {
  GIL::Lock();
  void *result = NULL;

  assert((PTH_INIT==1) && (RTL_INIT==1));
  assert(INIT == 0);
  DECLARE_TID_AND_PC();
  assert(INIT == 1);
  assert(tid != 0);
  assert(INFO.tid != 0);

  memset(INFO.TLEB, '\0', sizeof(INFO.TLEB));

  callback_arg *cb_arg = (callback_arg*)arg;
  pthread_worker *routine = cb_arg->routine;
  void *routine_arg = cb_arg->arg;
  pthread_attr_t attr;
  size_t stack_size = 8 << 20;  // 8M
  void *stack_top = NULL;

  // We already know the child pid -- get the parent condvar to signal.
  tid_t parent = cb_arg->parent;
  assert(ChildThreadStartConds.find(parent) != ChildThreadStartConds.end());
  ParentCond = ChildThreadStartConds[parent];
  ChildThreadStartConds.erase(parent);
  DDPrintf("Erasing ChildThreadStartConds[%d]\n", parent);

  // Get the stack size and stack top for the current thread.
  // TODO(glider): do something if pthread_getattr_np() is not supported.
  if (pthread_getattr_np(pthread_self(), &attr) == 0) {
    pthread_attr_getstack(&attr, &stack_top, &stack_size);
    pthread_attr_destroy(&attr);
  }

  for (int sig = 0; sig < NSIG; sig++) {
    pending_signal_flags[sig] = false;
  }
  have_pending_signals = false;

  SPut(THR_START, INFO.tid, 0, 0, parent);
  IN_RTL++;
  CHECK_IN_RTL();
  delete cb_arg;
  IN_RTL--;
  CHECK_IN_RTL();

  if (stack_top) {
    // We don't intercept the mmap2 syscall that allocates thread stack, so pass
    // the event to ThreadSanitizer manually.
    // TODO(glider): technically our parent allocates the stack. Maybe this
    // should be fixed and its tid should be passed in the MMAP event.
    // TODO(glider): we don't need MMAP at all. Remove it.
    SPut(MMAP, tid, pc, (uintptr_t)stack_top, stack_size);
    SPut(THR_STACK_TOP, tid, pc, (uintptr_t)stack_top, stack_size);
  } else {
    // Something's gone wrong. ThreadSanitizer will proceed, but if the stack
    // is reused by another thread, false positives will be reported.
    SPut(THR_STACK_TOP, tid, pc, (uintptr_t)&result, stack_size);
  }
  DDPrintf("Before routine() in T%d\n", tid);

  Finished[tid] = false;
#ifdef DEBUG
  dump_finished();
#endif
  GIL::Unlock();
  DDPrintf("pthread_cond_signal(ChildThreadStartConds[%d])\n", parent);
  __real_pthread_cond_signal(ParentCond);

  result = (*routine)(routine_arg);

  // We're about to stop the current thread. Block all the signals to prevent
  // invoking the handlers after THR_END is sent to ThreadSanitizer.
  sigfillset(&glob_sig_blocked);
  pthread_sigmask(SIG_BLOCK, &glob_sig_blocked, &glob_sig_old);

  GIL::Lock();
  Finished[tid] = true;
#ifdef DEBUG
  dump_finished();
#endif
  DDPrintf("After routine() in T%d\n", tid);

  // Call the TSD destructors set by pthread_key_create().
  int iter = PTHREAD_DESTRUCTOR_ITERATIONS;
  while (iter) {
    bool dirty = false;
    for (int i = 0; i < tsd_slot_index + 1; ++i) {
      // TODO(glider): we may want to delete keys associated with NULL values
      // from the map
      void *value = pthread_getspecific(tsd_slots[i].key);
      if (value) {
        dirty = true;
        // Associate NULL with the key and call the destructor.
        pthread_setspecific(tsd_slots[i].key, NULL);
        if (tsd_slots[i].dtor) (*tsd_slots[i].dtor)(value);
      }
    }
    iter--;
    if (!dirty) iter = 0;
  }

  // Flush all the events not flushed so far.
  flush_tleb();
  SPut(THR_END, tid, 0, 0, 0);
  if (FinishConds.find(tid) != FinishConds.end()) {
    DDPrintf("T%d (child of T%d): Signaling on %p\n", tid, parent, FinishConds[tid]);
    __real_pthread_cond_signal(FinishConds[tid]);
  } else {
    DDPrintf("T%d (child of T%d): Not signaling, condvar not ready\n", tid, parent);
  }
  // The parent is guaranteed not to wait for this thread to start, so it's ok
  // to deallocate ParentCond.
  __real_free(ParentCond);
  GIL::Unlock();
  return result;
}

void unsafe_forget_thread(tid_t tid, tid_t from) {
  DDPrintf("T%d: forgetting about T%d\n", from, tid);
  assert(PThreads.find(tid) != PThreads.end());
  pthread_t pt = PThreads[tid];
  Tids.erase(pt);
  PThreads.erase(tid);
  Finished.erase(tid);
  InitConds.erase(pt);
  FinishConds.erase(tid);
}


// To declare a wrapper for foo(bar) you should:
//  -- add the __wrap_foo(bar) prototype to tsan_rtl_wrap.h
//  -- implement __wrap_foo(bar) somewhere below using __real_foo(bar) as the
//     original function name
//  -- add foo to scripts/link_config.sh
//
// If you're wrapping a function that could potentially be called by
// ThreadSanitizer itself and/or by a signal handler, make sure it won't cause
// a deadlock. Use IN_RTL to check whether the wrapped function is called from
// the runtime library and fall back to the original version without emitting
// events or calling ThreadSanitizer routines. Always enclose potentially
// reentrant logic with IN_RTL++/IN_RTL--.
//
// To protect ThreadSanitizer's shadow stack wrappers should emit
// RTN_CALL/RTN_EXIT events.
//
// Rule of thumb: __wrap_foo should never make a tail call to __real_foo,
// because it normally should end with IN_RTL-- and RTN_EXIT.

extern "C"
int __wrap_pthread_create(pthread_t *thread,
                          pthread_attr_t *attr,
                          void *(*start_routine)(void*), void *arg) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_pthread_create, 0);
  IN_RTL++;
  CHECK_IN_RTL();
  callback_arg *cb_arg = new callback_arg;
  cb_arg->routine = start_routine;
  cb_arg->arg = arg;
  cb_arg->parent = tid;
  cb_arg->attr = attr;
  SPut(THR_CREATE_BEFORE, tid, 0, 0, 0);
  PTH_INIT = 1;
  pthread_cond_t *cond;
  {
    GIL scoped;
    // |cond| escapes to the child thread via ChildThreadStartConds, where it's
    // used to initialize ParentCond.
    cond = (pthread_cond_t*) __real_malloc(sizeof(pthread_cond_t));
    pthread_cond_init(cond, NULL);
    ChildThreadStartConds[tid] = cond;
    DDPrintf("Setting ChildThreadStartConds[%d]\n", tid);
  }
  IN_RTL--;
  CHECK_IN_RTL();
  int result = __real_pthread_create(thread, attr, pthread_callback, cb_arg);
  tid_t child_tid = 0;
  if (result == 0) {
    GIL scoped;
    if (Tids.find(*thread) == Tids.end()) {
      DDPrintf("pthread_cond_wait(ChildThreadStartConds[%d])\n", tid);
      __real_pthread_cond_wait(cond, &global_lock);
    } else {
      DDPrintf("Tids.find(%d) == Tids.end()", *thread);
      DDPrintf(", not waiting on ChildThreadStartConds[%d]\n", tid);
    }
    child_tid = Tids[*thread];
  }
  if (result) SPut(THR_CREATE_AFTER, tid, 0, 0, child_tid);
  DDPrintf("pthread_create(%p)\n", *thread);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}


inline void IGNORE_ALL_ACCESSES_BEGIN() {
  DECLARE_TID_AND_PC();
  Put(IGNORE_READS_BEG, tid, pc, 0, 0);
  Put(IGNORE_WRITES_BEG, tid, pc, 0, 0);
}

inline void IGNORE_ALL_ACCESSES_END() {
  DECLARE_TID_AND_PC();
  Put(IGNORE_READS_END, tid, pc, 0, 0);
  Put(IGNORE_WRITES_END, tid, pc, 0, 0);
}

inline void IGNORE_ALL_SYNC_BEGIN(void) {
  //TODO(glider): sync++
}

inline void IGNORE_ALL_SYNC_END(void) {
  //TODO(glider): sync--
}


void IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN(void) {
  IGNORE_ALL_ACCESSES_BEGIN();
  IGNORE_ALL_SYNC_BEGIN();
}

void IGNORE_ALL_ACCESSES_AND_SYNC_END(void) {
  IGNORE_ALL_ACCESSES_END();
  IGNORE_ALL_SYNC_END();
}

// Static initialization and pthread_once() {{{1
// TODO(glider): for each runtime static initialization a guard variable
// with a name like "_ZGVZN4testEvE1a" ("guard variable for test()::a")
// is created. It's more correct to emit a WAIT event upon each read of
// such a variable, and a SIGNAL upon each write (or the corresponding
// __cxa_guard_release() call.
extern "C"
int __wrap___cxa_guard_acquire(int *guard) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap___cxa_guard_acquire, 0);
  long result = __real___cxa_guard_acquire(guard);
  IGNORE_ALL_ACCESSES_BEGIN();
  if (!result) {
    IGNORE_ALL_ACCESSES_END();
  }
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int __wrap___cxa_guard_release(int *guard) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap___cxa_guard_release, 0);
  long result = __real___cxa_guard_release(guard);
  IGNORE_ALL_ACCESSES_END();
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int __wrap_pthread_once(pthread_once_t *once_control,
                        void (*init_routine) (void)) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_pthread_once, 0);
  IGNORE_ALL_ACCESSES_BEGIN();
  int result = __real_pthread_once(once_control, init_routine);
  IGNORE_ALL_ACCESSES_END();
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}
// }}}

// Memory allocation routines {{{1
extern "C"
void *__wrap_mmap(void *addr, size_t length, int prot, int flags,
                  int fd, off_t offset) {
  void *result;
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)addr, 0);
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  result = __real_mmap(addr, length, prot, flags, fd, offset);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  if (result != (void*) -1) {
    DECLARE_TID_AND_PC();
    SPut(MMAP, tid, pc, (uintptr_t)result, (uintptr_t)length);
  }
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int __wrap_munmap(void *addr, size_t length) {
  int result;
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)addr, 0);
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  result = __real_munmap(addr, length);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  if (result == 0) {
    SPut(MUNMAP, tid, pc, (uintptr_t)addr, (uintptr_t)length);
  }
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
void *__wrap_calloc(size_t nmemb, size_t size) {
  void *result;
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_calloc, 0);
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  result = __real_calloc(nmemb, size);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  SPut(MALLOC, tid, pc, (uintptr_t)result, nmemb * size);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
void *__wrap_malloc(size_t size) {
  if (IN_RTL) return __real_malloc(size);
  IN_RTL++;
  CHECK_IN_RTL();
  void *result;
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_malloc, 0);
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  result = __real_malloc(size);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  SPut(MALLOC, tid, pc, (uintptr_t)result, size);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  IN_RTL--;
  CHECK_IN_RTL();
  return result;
}

extern "C"
void __wrap_free(void *ptr) {
  if (IN_RTL) return __real_free(ptr);
  IN_RTL++;
  CHECK_IN_RTL();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_free, 0);
  // Normally pc is equal to 0, but FREE asserts that it is not.
  SPut(FREE, tid, (pc_t)__wrap_free, (uintptr_t)ptr, 0);
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  __real_free(ptr);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  IN_RTL--;
  CHECK_IN_RTL();
  RPut(RTN_EXIT, tid, pc, 0, 0);
}

extern "C"
void *__wrap_realloc(void *ptr, size_t size) {
  void *result;
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_realloc, 0);
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  result = __real_realloc(ptr, size);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  SPut(MALLOC, tid, pc, (uintptr_t)result, size);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

// }}}

// new/delete {{{1
#ifdef TSAN_RTL_X86
extern "C"
void *__wrap__Znwj(unsigned int size) {
  if (IN_RTL) return __real__Znwj(size);
  IN_RTL++;
  CHECK_IN_RTL();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap__Znwj, 0);
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  void *result = __real__Znwj(size);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  SPut(MALLOC, tid, pc, (uintptr_t)result, size);
  IN_RTL--;
  CHECK_IN_RTL();
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
void *__wrap__Znaj(unsigned int size) {
  if (IN_RTL) return __real__Znaj(size);
  IN_RTL++;
  CHECK_IN_RTL();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap__Znaj, 0);
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  void *result = __real__Znaj(size);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  SPut(MALLOC, tid, pc, (uintptr_t)result, size);
  IN_RTL--;
  CHECK_IN_RTL();
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}
#endif

#ifdef TSAN_RTL_X64
extern "C"
void *__wrap__Znwm(unsigned long size) {
  if (IN_RTL) return __real__Znwm(size);
  IN_RTL++;
  CHECK_IN_RTL();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap__Znwm, 0);
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  void *result = __real__Znwm(size);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  SPut(MALLOC, tid, pc, (uintptr_t)result, size);
  IN_RTL--;
  CHECK_IN_RTL();
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
void *__wrap__Znam(unsigned long size) {
  if (IN_RTL) return __real__Znam(size);
  IN_RTL++;
  CHECK_IN_RTL();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap__Znam, 0);
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  void *result = __real__Znam(size);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  SPut(MALLOC, tid, pc, (uintptr_t)result, size);
  IN_RTL--;
  CHECK_IN_RTL();
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

#endif


extern "C"
void __wrap__ZdlPv(void *ptr) {
  if (IN_RTL) {
    __real__ZdlPv(ptr);
    return;
  }
  IN_RTL++;
  CHECK_IN_RTL();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap__ZdlPv, 0);
  SPut(FREE, tid, (pc_t)__wrap__ZdlPv, (uintptr_t)ptr, 0);
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  __real__ZdlPv(ptr);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  IN_RTL--;
  CHECK_IN_RTL();
  RPut(RTN_EXIT, tid, pc, 0, 0);
}

extern "C"
void __wrap__ZdaPv(void *ptr) {
  if (IN_RTL) {
    __real__ZdaPv(ptr);
    return;
  }
  IN_RTL++;
  CHECK_IN_RTL();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap__ZdaPv, 0);
  SPut(FREE, tid, (pc_t)__wrap__ZdaPv, (uintptr_t)ptr, 0);
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  __real__ZdaPv(ptr);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  IN_RTL--;
  CHECK_IN_RTL();
  RPut(RTN_EXIT, tid, pc, 0, 0);
}
// }}}

// Unnamed POSIX semaphores {{{1
// TODO(glider): support AnnotateIgnoreSync here.

extern "C"
sem_t *__wrap_sem_open(const char *name, int oflag,
                mode_t mode, unsigned int value) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_sem_open, 0);
  sem_t *result = __real_sem_open(name, oflag, mode, value);
  if ((oflag & O_CREAT) &&
      value > 0 &&
      result != SEM_FAILED) {
    SPut(SIGNAL, tid, pc, (uintptr_t)result, 0);
  }
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int __wrap_sem_wait(sem_t *sem) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_sem_wait, 0);
  int result = __real_sem_wait(sem);
  if (result == 0) {
    SPut(WAIT, tid, pc, (uintptr_t)sem, 0);
  }
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int __wrap_sem_trywait(sem_t *sem) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_sem_wait, 0);
  int result = __real_sem_trywait(sem);
  if (result == 0) {
    SPut(WAIT, tid, pc, (uintptr_t)sem, 0);
  }
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int __wrap_sem_post(sem_t *sem) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_sem_post, 0);
  SPut(SIGNAL, tid, pc, (uintptr_t)sem, 0);
  int result = __real_sem_post(sem);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

// }}}

// libpthread wrappers {{{1
extern "C"
int __wrap_pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                                  const struct timespec *abstime) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_pthread_cond_timedwait, 0);
  SPut(UNLOCK, tid, pc, (uintptr_t)mutex, 0);
  int result = __real_pthread_cond_timedwait(cond, mutex, abstime);
  if (result == 0) {
    SPut(WAIT, tid, pc, (uintptr_t)cond, 0);
  }
  SPut(WRITER_LOCK, tid, pc, (uintptr_t)mutex, 0);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int __wrap_pthread_cond_signal(pthread_cond_t *cond) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_pthread_cond_signal, 0);
  int result = __real_pthread_cond_signal(cond);
  SPut(SIGNAL, tid, pc, (uintptr_t)cond, 0);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int __wrap_pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_pthread_cond_wait, 0);
  SPut(UNLOCK, tid, pc, (uintptr_t)mutex, 0);
  int result = __real_pthread_cond_wait(cond, mutex);
  SPut(WAIT, tid, pc, (uintptr_t)cond, 0);
  SPut(WRITER_LOCK, tid, pc, (uintptr_t)mutex, 0);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int __wrap_pthread_mutex_lock(pthread_mutex_t *mutex) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_pthread_mutex_lock, 0);
  int result = __real_pthread_mutex_lock(mutex);
  if (result == 0 /* success */) {
    SPut(WRITER_LOCK, tid, pc, (uintptr_t)mutex, 0);
  }
  // TODO(glider): should we handle error codes?
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int __wrap_pthread_mutex_trylock(pthread_mutex_t *mutex) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_pthread_mutex_trylock, 0);
  int result = __real_pthread_mutex_trylock(mutex);
  if (result == 0) {
    SPut(WRITER_LOCK, tid, pc, (uintptr_t)mutex, 0);
  }
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int __wrap_pthread_mutex_unlock(pthread_mutex_t *mutex) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_pthread_mutex_unlock, 0);
  SPut(UNLOCK, tid, pc, (uintptr_t) mutex, 0);
  int result = __real_pthread_mutex_unlock(mutex);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int __wrap_pthread_mutex_destroy(pthread_mutex_t *mutex) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_pthread_mutex_destroy, 0);
  SPut(LOCK_DESTROY, tid, pc, (uintptr_t)mutex, 0);  // before the actual call.
  int result = __real_pthread_mutex_destroy(mutex);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int __wrap_pthread_mutex_init(pthread_mutex_t *mutex,
                              const pthread_mutexattr_t *attr) {
  int result, mbRec;
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_pthread_mutex_init, 0);
  mbRec = 0; // unused so far
  if (attr) {
    int ty, zzz;
    zzz = pthread_mutexattr_gettype(attr, &ty);
    if (zzz == 0 && ty == PTHREAD_MUTEX_RECURSIVE) mbRec = 1;
  }
  result = __real_pthread_mutex_init(mutex, attr);
  if (result == 0) {
    SPut(LOCK_CREATE, tid, pc, (uintptr_t)mutex, 0);
  }
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int __wrap_pthread_rwlock_init(pthread_rwlock_t *rwlock,
                               const pthread_rwlockattr_t *attr) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_pthread_rwlock_init, 0);
  int result = __real_pthread_rwlock_init(rwlock, attr);
  if (result == 0) {
    SPut(LOCK_CREATE, tid, pc, (uintptr_t)rwlock, 0);
  }
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int __wrap_pthread_rwlock_destroy(pthread_rwlock_t *rwlock) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_pthread_rwlock_destroy, 0);
  SPut(LOCK_DESTROY, tid, pc, (uintptr_t)rwlock, 0);  // before the actual call.
  int result = __real_pthread_rwlock_destroy(rwlock);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int __wrap_pthread_rwlock_trywrlock(pthread_rwlock_t *rwlock) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_pthread_rwlock_trywrlock, 0);
  int result = __real_pthread_rwlock_trywrlock(rwlock);
  if (result == 0) {
    SPut(WRITER_LOCK, tid, pc, (uintptr_t)rwlock, 0);
  }
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int __wrap_pthread_rwlock_wrlock(pthread_rwlock_t *rwlock) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_pthread_rwlock_wrlock, 0);
  int result = __real_pthread_rwlock_wrlock(rwlock);
  if (result == 0) {
    SPut(WRITER_LOCK, tid, pc, (uintptr_t)rwlock, 0);
  }
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int __wrap_pthread_rwlock_tryrdlock(pthread_rwlock_t *rwlock) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_pthread_rwlock_tryrdlock, 0);
  int result = __real_pthread_rwlock_tryrdlock(rwlock);
  if (result == 0) {
    SPut(READER_LOCK, tid, pc, (uintptr_t)rwlock, 0);
  }
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int __wrap_pthread_rwlock_rdlock(pthread_rwlock_t *rwlock) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_pthread_rwlock_rdlock, 0);
  int result = __real_pthread_rwlock_rdlock(rwlock);
  if (result == 0) {
    SPut(READER_LOCK, tid, pc, (uintptr_t)rwlock, 0);
  }
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int __wrap_pthread_rwlock_unlock(pthread_rwlock_t *rwlock) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_pthread_rwlock_unlock, 0);
  SPut(UNLOCK, tid, pc, (uintptr_t)rwlock, 0);
  int result = __real_pthread_rwlock_unlock(rwlock);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int __wrap_pthread_barrier_init(pthread_barrier_t *barrier,
                         const pthread_barrierattr_t *attr, unsigned count) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_pthread_barrier_init, 0);
  SPut(CYCLIC_BARRIER_INIT, tid, pc, (uintptr_t)barrier, count);
  int result = __real_pthread_barrier_init(barrier, attr, count);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int __wrap_pthread_barrier_wait(pthread_barrier_t *barrier) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_pthread_barrier_wait, 0);
  SPut(CYCLIC_BARRIER_WAIT_BEFORE, tid, pc, (uintptr_t)barrier, 0);
  int result = __real_pthread_barrier_wait(barrier);
  SPut(CYCLIC_BARRIER_WAIT_AFTER, tid, pc, (uintptr_t)barrier, 0);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int __wrap_pthread_key_create(pthread_key_t *key,
                              void (*destr_function) (void *)) {
  // We don't want libpthread to know about the destructors.
  int result = __real_pthread_key_create(key, NULL);
  if (destr_function && (result == 0)) {
    tsd_slot_index++;
    // TODO(glider): we should delete TSD slots on pthread_key_delete.
    assert(tsd_slot_index < (int)(sizeof(tsd_slots) / sizeof(tsd_slot)));
    tsd_slots[tsd_slot_index].key = *key;
    tsd_slots[tsd_slot_index].dtor = destr_function;
  }
  return result;
}

extern "C"
int __wrap_pthread_join(pthread_t thread, void **value_ptr) {
  // Note that the ThreadInfo of |thread| is valid no more.
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_pthread_join, 0);
  tid_t joined_tid = -1;
  {
    GIL scoped;
    // TODO(glider): locking GIL should probably enforce IN_RTL++.
    IN_RTL++;
    CHECK_IN_RTL();
    if (Tids.find(thread) == Tids.end()) {
      InitConds[thread] = new pthread_cond_t;
      pthread_cond_init(InitConds[thread], NULL);
      DDPrintf("T%d: Initializing InitConds[%p]=%p\n", tid, thread, InitConds[thread]);
      DDPrintf("T%d (parent of %p): Waiting on InitConds[%p]=%p\n", tid, thread, thread, InitConds[thread]);
      __real_pthread_cond_wait(InitConds[thread], &global_lock);
    }
    assert (Tids.find(thread) != Tids.end());
    joined_tid = Tids[thread];
    DDPrintf("T%d: Finished[T%d]=%d\n", tid, joined_tid, Finished[joined_tid]);
    if (Finished.find(joined_tid) == Finished.end()) {
      Finished[joined_tid] = false;
      DDPrintf("T%d: setting Finished[T%d]=false\n", tid, joined_tid);
    }
    if (!Finished[joined_tid]) {
      FinishConds[joined_tid] = new pthread_cond_t;
      pthread_cond_init(FinishConds[joined_tid], NULL);
      DDPrintf("T%d: Initializing FinishConds[%d]=%p\n", tid, joined_tid, FinishConds[joined_tid]);
      DDPrintf("T%d (parent of T%d): Waiting on FinishConds[%d]=%p\n", tid, joined_tid, joined_tid, FinishConds[joined_tid]);
      __real_pthread_cond_wait(FinishConds[joined_tid], &global_lock);
    }
    unsafe_forget_thread(joined_tid, tid); // TODO(glider): earlier?
    IN_RTL--;
    CHECK_IN_RTL();
  }

  int result = __real_pthread_join(thread, value_ptr);
  {
    assert(joined_tid > 0);
    pc_t pc = GetPc();
    SPut(THR_JOIN_AFTER, tid, pc, joined_tid, 0);
  }
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int __wrap_pthread_spin_init(pthread_spinlock_t *lock, int pshared) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_pthread_spin_init, 0);
  int result = __real_pthread_spin_init(lock, pshared);
  if (result == 0) {
    SPut(UNLOCK_OR_INIT, tid, pc, (uintptr_t)lock, 0);
  }
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int __wrap_pthread_spin_destroy(pthread_spinlock_t *lock) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_pthread_spin_destroy, 0);
  SPut(LOCK_DESTROY, tid, pc, (uintptr_t)lock, 0);
  int result = __real_pthread_spin_destroy(lock);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}


extern "C"
int __wrap_pthread_spin_lock(pthread_spinlock_t *lock) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_pthread_spin_lock, 0);
  int result = __real_pthread_spin_lock(lock);
  if (result == 0) {
    SPut(WRITER_LOCK, tid, pc, (uintptr_t)lock, 0);
  }
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int __wrap_pthread_spin_trylock(pthread_spinlock_t *lock) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_pthread_spin_trylock, 0);
  int result = __real_pthread_spin_trylock(lock);
  if (result == 0) {
    SPut(WRITER_LOCK, tid, pc, (uintptr_t)lock, 0);
  }
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int __wrap_pthread_spin_unlock(pthread_spinlock_t *lock) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_pthread_spin_unlock, 0);
  SPut(UNLOCK, tid, pc, (uintptr_t)lock, 0);
  int result = __real_pthread_spin_unlock(lock);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}


// }}}

// STR* wrappers {{{1
extern "C"
char *__wrap_strchr(const char *s, int c) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_strchr, 0);
  pc = (pc_t)__wrap_strchr;
  char *result = Replace_strchr(tid, pc, s, c);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
char *__wrap_strrchr(const char *s, int c) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_strrchr, 0);
  pc = (pc_t)__wrap_strrchr;
  char *result = Replace_strrchr(tid, pc, s, c);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
size_t __wrap_strlen(const char *s) {
  if (IN_RTL) return __real_strlen(s);
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_strlen, 0);
  IN_RTL++;
  CHECK_IN_RTL();
  pc = (pc_t)__wrap_strlen;
  size_t result = Replace_strlen(tid, pc, s);
  IN_RTL--;
  CHECK_IN_RTL();
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

// TODO(glider): do we need memcpy?
// The instrumentation pass replaces llvm.memcpy and llvm.memmove
// with calls to rtl_memcpy and rtl_memmove, but some prior optimizations may
// convert the intrinsics into calls to memcpy() and memmove().
extern "C"
char *__wrap_memcpy(char *dest, const char *src, size_t n) {
  if (IN_RTL) return __real_memcpy(dest, src, n);
  DECLARE_TID_AND_PC();
  IN_RTL++;
  CHECK_IN_RTL();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_memcpy, 0);
  pc = (pc_t)__wrap_memcpy;
  char *result = Replace_memcpy(tid, pc, dest, src, n);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  IN_RTL--;
  CHECK_IN_RTL();
  return result;
}

extern "C"
void *__wrap_memmove(char *dest, const char *src, size_t n) {
  if (IN_RTL) return __real_memmove(dest, src, n);
  DECLARE_TID_AND_PC();
  pc = (pc_t)__wrap_memmove;
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_memmove, 0);
  IN_RTL++;
  CHECK_IN_RTL();
  void *result = __real_memmove(dest, src, n);
  REPORT_READ_RANGE(src, n);
  REPORT_WRITE_RANGE(dest, n);
  IN_RTL--;
  CHECK_IN_RTL();
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}


// TODO(glider): ???
extern
char *strcpy(char *dest, const char *src) {
  DECLARE_TID_AND_PC();
  pc = (pc_t)strcpy;
  return Replace_strcpy(tid, pc, dest, src);
}

extern "C"
void *__wrap_memchr(const char *s, int c, size_t n) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_memchr, 0);
  pc = (pc_t)__wrap_memchr;
  void *result = Replace_memchr(tid, pc, s, c, n);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

#ifdef TSAN_RTL_X86
extern
void *memchr(void *s, int c, unsigned int n) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_memchr, 0);
  pc = (pc_t)__wrap_memchr;
  void *result = Replace_memchr(tid, pc, (char*)s, c, n);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}
#endif

#ifdef TSAN_RTL_X64
extern
void *memchr(void *s, int c, unsigned long n) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_memchr, 0);
  pc = (pc_t)__wrap_memchr;
  void *result = Replace_memchr(tid, pc, (char*)s, c, n);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}
#endif

extern "C"
int __wrap_strcmp(const char *s1, const char *s2) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_strcmp, 0);
  pc = (pc_t)__wrap_strcmp;
  int result = Replace_strcmp(tid, pc, s1, s2);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int strncmp(const char *s1, const char *s2, size_t n) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)strncmp, 0);
  pc = (pc_t)strncmp;
  int result = Replace_strncmp(tid, pc, s1, s2, n);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

// }}}

// atexit() and exit() wrappers. {{{1
// atexit -> exit is a happens-before arc.
static const uintptr_t kAtExitMagic = 0x12345678;

typedef void atexit_worker(void);

// TODO(glider): sysconf(_SC_ATEXIT_MAX)
#define ATEXIT_MAX 32
atexit_worker* atexit_stack[ATEXIT_MAX];
int atexit_index = -1;
void push_atexit(atexit_worker *worker) {
  GIL scoped;
  atexit_index++;
  assert(atexit_index < ATEXIT_MAX);
  atexit_stack[atexit_index] = worker;
}

atexit_worker* pop_atexit() {
  GIL scoped;
  assert(atexit_index > -1);
  return atexit_stack[atexit_index--];
}

void atexit_callback() {
  DECLARE_TID_AND_PC();
  atexit_worker *worker = pop_atexit();
  SPut(WAIT, tid, pc, (uintptr_t)worker, 0);
  (*worker)();
}

extern "C"
int __wrap_atexit(void (*function)(void)) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_atexit, 0);
  push_atexit(function);
  int result = __real_atexit(atexit_callback);
  SPut(SIGNAL, tid, pc, kAtExitMagic, 0);  // TODO(glider): do we need it?
  SPut(SIGNAL, tid, pc, (uintptr_t)function, 0);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
void __wrap_exit(int status) {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_exit, 0);
  SPut(WAIT, tid, pc, kAtExitMagic, 0);
  __real_exit(status);
  // This is in fact never called.
  RPut(RTN_EXIT, tid, pc, 0, 0);
}

// }}}

extern "C"
pid_t __wrap_fork() {
  pid_t result;
  IN_RTL++;
  CHECK_IN_RTL();
  result = __real_fork();
  IN_RTL--;
  CHECK_IN_RTL();
  return result;
}
// Happens-before arc between read() and write() {{{1
const uintptr_t kFdMagicConst = 0xf11ede5c;
uintptr_t FdMagic(int fd) {
  uintptr_t result = kFdMagicConst;
  struct stat stat_b;
  fstat(fd, &stat_b);
  if (stat_b.st_dev) result = (uintptr_t)stat_b.st_dev;
  return result;
}

extern "C"
ssize_t __wrap_read(int fd, void *buf, size_t count) {
  ssize_t result = __real_read(fd, buf, count);
  if (IN_RTL) return result;
  IN_RTL++;
  CHECK_IN_RTL();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_read, 0);
  // TODO(glider): we should treat dup()ped fds as equal ones.
  // It only makes sense to wait for a previous write, not EOF.
  if (result > 0) {
    SPut(WAIT, tid, pc, FdMagic(fd), 0);
  }
  IN_RTL--;
  CHECK_IN_RTL();
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
ssize_t __wrap_write(int fd, const void *buf, size_t count) {
  if (IN_RTL) return __real_write(fd, buf, count);
  IN_RTL++;
  CHECK_IN_RTL();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_write, 0);
  SPut(SIGNAL, tid, pc, FdMagic(fd), 0);
  ssize_t result = __real_write(fd, buf, count);
  IN_RTL--;
  CHECK_IN_RTL();
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}
// }}}

// Signal handling {{{1
/* Initial support for signals. Each user signal handler is stored in
 signal_actions[] and RTLSignalHandler is installed instead. When a signal is
 received, it is put into a thread-local array of pending signals (see the
 comments in RTLSignalHandler). Each time we're about to release the global
 lock, we handle all the pending signals.
*/
inline int unsafe_clear_pending_signals() {
  if (!have_pending_signals) return 0;
  int result = 0;
  for (int sig = 0; sig < NSIG; sig++) {
    if (pending_signal_flags[sig]) {
      DPrintf("[T%d] Pending signal: %d\n", GetTid(), sig);
      sigfillset(&glob_sig_blocked);
      pthread_sigmask(SIG_BLOCK, &glob_sig_blocked, &glob_sig_old);
      pending_signal_flags[sig] = false;
      signal_actions[sig].sa_sigaction(sig, &pending_signals[sig], NULL);
      pthread_sigmask(SIG_SETMASK, &glob_sig_old, &glob_sig_old);
      result++;
    }
  }
  have_pending_signals = false;
  return result;
}

extern "C"
void RTLSignalHandler(int sig, siginfo_t* info, void* context) {
  /* TODO(glider): The code under "#if 0" assumes that it's legal to handle
   * signals on the thread running client code. In fact ThreadSanitizer calls
   * some non-reentrable routines, so if a signal is received when the client
   * code is inside them a deadlock may happen. A temporal solution is to always
   * enqueue the signals. In the future we can get rid of such calls within
   * ThreadSanitzer. */
#if 0
  if (IN_RTL == 0) {
#else
  if (0) {
#endif
    // We're in the client code. Call the handler.
    signal_actions[sig].sa_sigaction(sig, info, context);
  } else {
    // We're in TSan code. Let's enqueue the signal
    if (!pending_signal_flags[sig]) {
      pending_signals[sig] = *info;
      pending_signal_flags[sig] = true;
      have_pending_signals = true;
    }
  }
}

// TODO(glider): wrap signal()
extern "C"
int __wrap_sigaction(int signum, const struct sigaction *act,
                     struct sigaction *oldact) {
  GIL scoped;
  int result;
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_sigaction, 0);
  if ((act->sa_handler == SIG_IGN) || (act->sa_handler == SIG_DFL)) {
   result = __real_sigaction(signum, act, oldact);
  } else {
    signal_actions[signum] = *act;
    struct sigaction new_act = *act;
    new_act.sa_sigaction = RTLSignalHandler;
    result = __real_sigaction(signum, &new_act, oldact);
  }
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

// }}}

// instrumentation API {{{1
extern "C"
void rtn_call(void *addr) {
  // TODO(glider): this is unnecessary if we flush before each call/invoke
  // insn.
  flush_tleb();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)addr, 0);
}

extern "C"
void rtn_exit() {
  flush_tleb();
  DECLARE_TID_AND_PC();
  RPut(RTN_EXIT, tid, pc, 0, 0);
}

// TODO(glider): we may want the basic block address to differ from the PC
// of the first MOP in that basic block.
extern "C"
void* bb_flush(TraceInfoPOD *next_mops) {
  if (INFO.trace_info) {
    // This is not a function entry block
    flush_trace();
  }
  INFO.trace_info = next_mops;
  return (void*) INFO.TLEB;
}
extern "C"
void *rtl_memcpy(char *dest, const char *src, size_t n) {
  DECLARE_TID_AND_PC();
  pc = (pc_t)rtl_memcpy;
  return Replace_memcpy(tid, pc, dest, src, n);
}

extern "C"
void *rtl_memmove(char *dest, const char *src, size_t n) {
  DECLARE_TID_AND_PC();
  void *result = __real_memmove(dest, src, n);
  pc = (pc_t)rtl_memmove;
  REPORT_READ_RANGE(src, n);
  REPORT_WRITE_RANGE(dest, n);
  return result;
}
// }}}


// Debug info routines {{{1
struct PcInfo {
  uintptr_t pc;
  uintptr_t symbol;
  uintptr_t path;
  uintptr_t file;
  uintptr_t line;
};

void ReadDbgInfoFromSection(char* start, char* end) {
  static const int kDebugInfoMagicNumber = 0xdb914f0;
  char *p = start;
  debug_info = new map<pc_t, LLVMDebugInfo>;
  while (p < end) {
    while ((p < end) && (*((int*)p) != kDebugInfoMagicNumber)) p++;
    if (p >= end) break;
    uintptr_t *head = (uintptr_t*)p;
    uintptr_t paths_size = head[1];
    uintptr_t files_size = head[2];
    uintptr_t symbols_size = head[3];
    uintptr_t pcs_size = head[4];
    p += 5 * sizeof(uintptr_t);

    char *paths_raw = p;
    map<uintptr_t, string> paths;
    uintptr_t paths_count = 0;
    char *pstart = paths_raw;
    for (size_t i = 0; i < paths_size; i++) {
      if (!pstart) pstart = paths_raw + i;
      if (paths_raw[i] == '\0') {
        paths[paths_count++] = string(pstart);
        DDPrintf("path: %s\n", paths[paths_count-1].c_str());
        pstart = NULL;
      }
    }

    char *files_raw = paths_raw + paths_size;
    map<uintptr_t, string> files;
    uintptr_t files_count = 0;
    char *fstart = files_raw;
    for (size_t i = 0; i < files_size; i++) {
      if (!fstart) fstart = files_raw + i;
      if (files_raw[i] == '\0') {
        files[files_count++] = string(fstart);
        DDPrintf("file: %s\n", files[files_count-1].c_str());
        fstart = NULL;
      }
    }

    map<uintptr_t, string> symbols;
    uintptr_t symbols_count = 0;
    char *symbols_raw = files_raw + files_size;
    char *sstart = symbols_raw;
    for (size_t i = 0; i < symbols_size; i++) {
      if (!sstart) sstart = symbols_raw + i;
      if (symbols_raw[i] == '\0') {
        symbols[symbols_count++] = string(sstart);
        DDPrintf("symbol: %s\n", symbols[symbols_count-1].c_str());
        sstart = NULL;
      }
    }
    size_t pad = (uintptr_t)(symbols_raw + symbols_size) % sizeof(uintptr_t);
    if (pad) pad = sizeof(uintptr_t) - pad;
    PcInfo *pcs = (PcInfo*)(symbols_raw + symbols_size + pad);
    for (size_t i = 0; i < pcs_size; i++) {
      DDPrintf("pc: %p, sym: %s, file: %s, path: %s, line: %d\n",
             pcs[i].pc, symbols[pcs[i].symbol].c_str(),
             files[pcs[i].file].c_str(), paths[pcs[i].path].c_str(),
             pcs[i].line);
#ifdef DEBUG
      assert((*debug_info).find(pcs[i].pc) == (*debug_info).end());
#endif
      (*debug_info)[pcs[i].pc].pc = pcs[i].pc;
      (*debug_info)[pcs[i].pc].symbol = symbols[pcs[i].symbol];
      (*debug_info)[pcs[i].pc].file = files[pcs[i].file];
      (*debug_info)[pcs[i].pc].path = paths[pcs[i].path];
      // TODO(glider): move the path-related logic to the compiler.
      if ((files[pcs[i].file] != "")  && (paths[pcs[i].path] != "")) {
        if (paths[pcs[i].path][paths[pcs[i].path].size() - 1] != '/') {
          (*debug_info)[pcs[i].pc].fullpath =
              paths[pcs[i].path] + "/" + files[pcs[i].file];
        } else {
          (*debug_info)[pcs[i].pc].fullpath =
              paths[pcs[i].path] + files[pcs[i].file];
        }
      } else {
        (*debug_info)[pcs[i].pc].fullpath = files[pcs[i].file];
      }
      (*debug_info)[pcs[i].pc].line = pcs[i].line;
    }
  }
}

void ReadElf() {
  const int kBufSize = 1000;
  char fname[kBufSize];
  memset(fname, '\0', sizeof(fname));
  int fsize = readlink("/proc/self/exe", fname, kBufSize);
  assert(fsize < kBufSize);
  int fd = open(fname, 0);
  struct stat st;
  fstat(fd, &st);
  DDPrintf("Reading debug info from %s (%d bytes)\n", fname, st.st_size);
  char* map = (char*)__real_mmap(NULL, st.st_size,
                                 PROT_READ, MAP_PRIVATE, fd, 0);

#ifdef TSAN_RTL_X86
  typedef Elf32_Ehdr Elf_Ehdr;
  typedef Elf32_Shdr Elf_Shdr;
  typedef Elf32_Off Elf_Off;
  typedef Elf32_Word Elf_Word;
#else
  typedef Elf64_Ehdr Elf_Ehdr;
  typedef Elf64_Shdr Elf_Shdr;
  typedef Elf64_Off Elf_Off;
  typedef Elf64_Word Elf_Word;
#endif
  Elf_Ehdr* ehdr = (Elf_Ehdr*)map;
  Elf_Shdr* shdrs = (Elf_Shdr*)(map + ehdr->e_shoff);
  char *hdr_strings = map + shdrs[ehdr->e_shstrndx].sh_offset;
  int shnum = ehdr->e_shnum;

  char* debug_info_section = NULL;
  size_t debug_info_size = 0;

  IN_RTL++;
  CHECK_IN_RTL();
  for (int i = 0; i < shnum; ++i) {
    Elf_Shdr* shdr = shdrs + i;
    Elf_Off off = shdr->sh_offset;
    Elf_Word name = shdr->sh_name;
    Elf_Word size = shdr->sh_size;
    DDPrintf("Section name: %d, %s\n", name ,hdr_strings + name);
    if (strcmp(hdr_strings + name, "tsan_rtl_debug_info") == 0) {
      debug_info_section = map + off;
      debug_info_size = size;
      break;
    }
  }
  IN_RTL--;
  CHECK_IN_RTL();
  assert(debug_info_section);
  // Parse the debug info section.
  ReadDbgInfoFromSection(debug_info_section,
                         debug_info_section + debug_info_size);
  // Finalize.
  __real_munmap(map, st.st_size);
  close(fd);
}

void ReadDbgInfo(string filename) {
  DbgInfoLock scoped;
  debug_info = new map<pc_t, LLVMDebugInfo>;
  string contents = ReadFileToString(filename, false);
  vector<string> lines;
  SplitString(contents, '\n', &lines, false);
  for (size_t i = 0; i < lines.size(); ++i) {
    vector<string> parts;
    SplitString(lines[i], '|', &parts, true);
    LLVMDebugInfo info;
    info.pc = strtol(parts[0].c_str(), NULL, 16); // TODO(glider): error code
    if (parts[3] != "") {
      info.line = strtol(parts[3].c_str(), NULL, 10);
    } else {
      info.line = 0;
    }
    info.symbol = parts[1];
    info.file = parts[2];
    info.path = parts[4];
    (*debug_info)[info.pc] = info;
  }
}

string PcToRtnName(pc_t pc, bool demangle) {
  DbgInfoLock scoped;
  if (debug_info && (debug_info->find(pc) != debug_info->end())) {
    return (*debug_info)[pc].symbol;
  }
  return "";
}

void PcToStrings(pc_t pc, bool demangle,
                 string *img_name, string *rtn_name,
                 string *file_name, int *line_no) {
  DbgInfoLock scoped;
  if (debug_info && (debug_info->find(pc) != debug_info->end())) {
    img_name = NULL;
    *rtn_name = (*debug_info)[pc].symbol;
    *file_name = ((*debug_info)[pc].fullpath);
    *line_no = ((*debug_info)[pc].line);
  } else {
    img_name = NULL;
    rtn_name = NULL;
    file_name = NULL;
    line_no = NULL;
  }
}
// }}}
