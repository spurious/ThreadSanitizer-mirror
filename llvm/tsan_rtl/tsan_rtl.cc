#include "tsan_rtl.h"

#include "ts_trace_info.h"
#include "ts_lock.h"

#include <elf.h>
#include <sys/mman.h>
#include <sys/stat.h>

#ifdef TSAN_RTL_X64
#include <asm/prctl.h>
#include <sys/prctl.h>
#endif

#include <map>
#include <vector>

#if defined(__GNUC__)
# include <cxxabi.h>  // __cxa_demangle
#endif

#define EXTRA_REPLACE_PARAMS tid_t tid, pc_t pc,
#define REPORT_READ_RANGE(x, size) do { \
    if (size) SPut(READ, tid, pc, (uintptr_t)(x), (size)); } while (0)
#define REPORT_WRITE_RANGE(x, size) do { \
    if (size) SPut(WRITE, tid, pc, (uintptr_t)(x), (size)); } while (0)
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

#if (DEBUG)
# define DPrintf(params...) \
    Printf(params)
# define DEBUG_DO(code) \
  do { code } while (0)
#else
# define DPrintf(params...)
# define DEBUG_DO(code)
#endif

int static_tls_size;

extern bool global_ignore;
bool FORKED_CHILD = false;  // if true, cannot access other threads' TLS
__thread int thread_local_ignore;
__thread bool thread_local_show_stats;
__thread int thread_local_literace;

struct ThreadInfo {
  tid_t tid;
  int *thread_local_ignore;
};

struct LLVMDebugInfo {
  LLVMDebugInfo()
    : pc(0), line(0) { }
  pc_t pc;
  string symbol;
  string demangled_symbol;
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
__thread tid_t LTID;  // literace TID = TID % kLiteRaceNumTids
__thread CallStackPod ShadowStack;
static const size_t kTLEBSize = 2000;
__thread uintptr_t TLEB[kTLEBSize];
__thread int INIT = 0;
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

std::map<pthread_t, ThreadInfo*> ThreadInfoMap;
std::map<pthread_t, tid_t> Tids;
std::map<tid_t, pthread_t> PThreads;
std::map<tid_t, bool> Finished;
// TODO(glider): before spawning a new child thread its parent creates a
// pthread barrier which is used to guarantee that the child has already
// initialized before exiting pthread_create.
std::map<tid_t, pthread_barrier_t*> ChildThreadStartBarriers;
// TODO(glider): we shouldn't need InitConds (and maybe FinishConds).
// How about using barriers here as well?
std::map<pthread_t, pthread_cond_t*> InitConds;
std::map<tid_t, pthread_cond_t*> FinishConds;
tid_t max_tid;

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
// #define BLOCK_SIGNALS 1
#undef BLOCK_SIGNALS

pthread_mutex_t global_lock = PTHREAD_MUTEX_INITIALIZER;
#define GIL_LOCK __real_pthread_mutex_lock
#define GIL_UNLOCK __real_pthread_mutex_unlock
#define GIL_TRYLOCK __real_pthread_mutex_trylock

pthread_t gil_owner = 0;
__thread int gil_depth = 0;

// Reentrancy counter {{{1
__thread int IN_RTL = 0;
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
#if (DEBUG)
  gil_owner = pthread_self();
#endif
  gil_depth++;
}

bool GIL::TryLock() {
#ifdef BLOCK_SIGNALS
  sigfillset(&glob_sig_blocked);
  pthread_sigmask(SIG_BLOCK, &glob_sig_blocked, &glob_sig_old);
#endif
#if (DEBUG)
  gil_owner = pthread_self();
#endif
  bool result;
  if (!gil_depth) {
    result = !static_cast<bool>(GIL_TRYLOCK(&global_lock));
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
    gil_depth--;
#if (DEBUG)
    gil_owner = 0;
#endif
#ifdef ENABLE_STATS
    if (UNLIKELY(G_flags->verbosity)) {
      if (stats_cur_events < kNumBuckets) {
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
#if (DEBUG)
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

INLINE void SPut(EventType type, tid_t tid, pc_t pc,
                 uintptr_t a, uintptr_t info) {
  DCHECK(HAVE_THREAD_0 || ((type == THR_START) && (tid == 0)));
  DCHECK(RTL_INIT == 1);

  if (type != THR_START) flush_tleb();
  Event event(type, tid, pc, a, info);
  if (G_flags->verbosity) {
    if ((G_flags->verbosity >= 2) ||
        (type == THR_START) ||
        (type == THR_END) ||
        (type == THR_JOIN_AFTER) ||
        (type == THR_CREATE_BEFORE)) {
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
  if (type == THR_START) {
    if (tid == 0) HAVE_THREAD_0 = 1;
  }
}

void INLINE flush_trace(TraceInfoPOD *trace) {
  DCHECK((size_t)(ShadowStack.end_ - ShadowStack.pcs_) > 0);
  DCHECK((size_t)(ShadowStack.end_ - ShadowStack.pcs_) < kMaxCallStackSize);
#if (DEBUG)
  // TODO(glider): PutTrace shouldn't be called without a lock taken.
  // However flushing events from unsafe_clear_pending_signals (called from
  // GIL::Unlock) is done under a mutex.
//  assert(!GIL::GetDepth());
#endif
  DCHECK(RTL_INIT == 1);
  if (!thread_local_ignore) {
    tid_t tid = INFO.tid;
    DCHECK(trace);
#ifdef ENABLE_STATS
    stats_events_processed += trace->n_mops_;
    stats_cur_events += trace->n_mops_;
#endif

    // Increment the trace counter in a racey way. This can lead to small
    // deviations if the trace is hot, but we can afford them.
    // Unfortunately this also leads to cache ping-pong and may affect the
    // performance.
    if (DEBUG && G_flags->show_stats) trace->counter_++;
    TraceInfo *trace_info = reinterpret_cast<TraceInfo*>(trace);

    // We optimize for --literace_sampling to be the default mode.
    // Possible values:
    // -- 1
    // -- G_flags->literace_sampling
    // -- thread_local_literace
    if (thread_local_literace) {
      trace_info->LLVMLiteRaceUpdate(LTID,
                               thread_local_literace);
    }
    if (DEBUG && G_flags->verbosity >= 2) {
      IN_RTL++;
      CHECK_IN_RTL();
      Event sblock(SBLOCK_ENTER, tid, trace->pc_, 0, trace->n_mops_);
      sblock.Print();
      DCHECK(trace->n_mops_);
      for (size_t i = 0; i < trace->n_mops_; i++) {
        if (trace->mops_[i].is_write()) {
          Event event(WRITE,
                      tid, trace->mops_[i].pc(),
                      TLEB[i], trace->mops_[i].size());
          event.Print();
        } else {
          Event event(READ,
                      tid, trace->mops_[i].pc(),
                      TLEB[i], trace->mops_[i].size());
          event.Print();
        }
      }
      IN_RTL--;
      CHECK_IN_RTL();
    }
    {
      IN_RTL++;
      CHECK_IN_RTL();
      DCHECK(ShadowStack.pcs_ <= ShadowStack.end_);
      ThreadSanitizerHandleTrace(tid,
                                 trace_info,
                                 TLEB);
      IN_RTL--;
      CHECK_IN_RTL();
    }

    // TODO(glider): the instrumentation pass may generate basic blocks that
    // are larger than sizeof(TLEB). There should be a flag to control this,
    // because we don't want to check the trace size in runtime.
    DCHECK(trace->n_mops_ <= kTLEBSize);
    // Check that ThreadSanitizer cleans up the TLEB.
    if (DEBUG) {
      for (size_t i = 0; i < trace->n_mops_; i++) DCHECK(TLEB[i] == 0);
    }
    unsafe_clear_pending_signals();
  }
}

INLINE void Put(EventType type, tid_t tid, pc_t pc,
                uintptr_t a, uintptr_t info) {
  DCHECK(!isThreadLocalEvent(type));
  SPut(type, tid, pc, a, info);
}

// RPut is strictly for putting RTN_CALL and RTN_EXIT events.
INLINE void RPut(EventType type, tid_t tid, pc_t pc,
                 uintptr_t a, uintptr_t info) {
  DCHECK(HAVE_THREAD_0 || ((type == THR_START) && (tid == 0)));
  DCHECK(RTL_INIT == 1);
  if (type == RTN_CALL) {
    rtn_call((void*)a);
  } else {
    rtn_exit();
  }
  unsafe_clear_pending_signals();
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
  for (int i = 0; i < kNumBuckets; i++) {
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

INLINE void init_debug() {
  CHECK(DBG_INIT == 0);
  char *dbg_info = getenv("TSAN_DBG_INFO");
  if (dbg_info) {
    ReadDbgInfo(dbg_info);
  } else {
    ReadElf();
    AddWrappersDbgInfo();
  }
  DBG_INIT = 1;
}

bool in_initialize = false;

#ifdef TSAN_RTL_X64
// TODO(glider): maybe use inline assembly instead?
extern "C" int arch_prctl(int code, unsigned long *addr);
#endif

// Tell the tool about the static TLS location. We assume that GIL is taken
// already.
// TODO(glider): handle the dynamic TLS.
void unsafeMapTls(tid_t tid, pc_t pc) {
  // According to "ELF Handling For Thread-Local Storage"
  // (http://www.akkadia.org/drepper/tls.pdf),
  // the static thread-local storage for x86 and x86-64 is located before the
  // TCB (stored in %gs or %fs, respectively). Its size is equal to the total
  // size of .tbss and .tdata sections of the binary.
  // We notify ThreadSanitizer about the static TLS location each time a new
  // thread is started to prevent false positives on reused TLS.
  // The TLS may be not contained in the thread stack, so clearing the stack is
  // not enough.
#ifdef TSAN_RTL_X86
  // TODO(glider): find the TSD address somehow. arch_prctl() is only available
  // on x86-64.
#endif
#ifdef TSAN_RTL_X64
  unsigned long tsd = 0;
  arch_prctl(ARCH_GET_FS, &tsd);
  unsigned long low = (tsd - static_tls_size);
  SPut(MMAP, tid, pc, low, static_tls_size);
#endif
}

bool initialize() {
  if (in_initialize) return false;
  if (RTL_INIT == 1) return true;
  in_initialize = true;

  // TODO(glider): do we need it?
  // assert(IN_RTL == 0);
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
    while (start != string::npos || stop != string::npos) {
      args.push_back(env_args.substr(start, stop - start));
      start = env_args.find_first_not_of(" ", stop);
      stop = env_args.find_first_of(" ", start);
    }
  }
#ifdef ENABLE_STATS
  for (int i = 0; i < kNumBuckets; i++) {
    stats_event_buckets[i] = 0;
  }
#endif

  ThreadSanitizerParseFlags(&args);
  ThreadSanitizerInit();
  init_debug();
  if (G_flags->dry_run) {
    Printf("WARNING: the --dry_run flag is not supported anymore. "
           "Ignoring.\n");
  }
  IN_RTL--;
  CHECK_IN_RTL();
  __real_atexit(finalize);
  RTL_INIT = 1;
  ShadowStack.end_ = ShadowStack.pcs_;
  in_initialize = false;
  // Get the stack size and stack top for the current thread.
  // TODO(glider): do something if pthread_getattr_np() is not supported.
  pthread_attr_t attr;
  size_t stack_size = 8 << 20;  // 8M
  void *stack_top = NULL;
  if (pthread_getattr_np(pthread_self(), &attr) == 0) {
    pthread_attr_getstack(&attr, &stack_top, &stack_size);
    pthread_attr_destroy(&attr);
  }

  for (int sig = 0; sig < NSIG; sig++) {
    pending_signal_flags[sig] = false;
  }
  have_pending_signals = false;

  SPut(THR_START, 0, (pc_t) &ShadowStack, 0, 0);
  if (stack_top) {
    // We don't intercept the mmap2 syscall that allocates thread stack, so pass
    // the event to ThreadSanitizer manually.
    // TODO(glider): technically our parent allocates the stack. Maybe this
    // should be fixed and its tid should be passed in the MMAP event.
    SPut(THR_STACK_TOP, 0, 0, (uintptr_t)stack_top, stack_size);
  } else {
    // Something's gone wrong. ThreadSanitizer will proceed, but if the stack
    // is reused by another thread, false positives will be reported.
    SPut(THR_STACK_TOP, 0, 0, (uintptr_t)&stack_size, stack_size);
  }
  unsafeMapTls(0, 0);
  return true;
}

// TODO(glider): GetPc should return valid PCs.
INLINE pc_t GetPc() {
  return 0;
}

extern pc_t ExGetPc() {
  return GetPc();
}

// Should be called under the global lock.
INLINE void UnsafeInitTidCommon() {
  IN_RTL++;
  CHECK_IN_RTL();
  memset(TLEB, 0, kTLEBSize);
  INFO.thread_local_ignore = &thread_local_ignore;
  thread_local_ignore = !!global_ignore;
  thread_local_show_stats = G_flags->show_stats;
  thread_local_literace = G_flags->literace_sampling;
  LTID = (INFO.tid % TraceInfoPOD::kLiteRaceNumTids);
  IN_RTL--;
  CHECK_IN_RTL();
  INIT = 1;
}

INLINE void InitRTLAndTid0() {
  CHECK(INIT == 0);
  GIL scoped;
  CHECK(RTL_INIT == 0);
  // Initialize ThreadSanitizer et. al.
  if (!initialize()) __real_exit(2);
  RTL_INIT = 1;
  // Initialize thread #0.
  INFO.tid = 0;
  max_tid = 1;
  UnsafeInitTidCommon();
}

INLINE void InitTid() {
  DCHECK(RTL_INIT == 1);
  GIL scoped;
  // thread initialization
  pthread_t pt = pthread_self();
  INFO.tid = max_tid;
  max_tid++;
  // TODO(glider): remove InitConds.
  if (InitConds.find(pt) != InitConds.end()) {
    __real_pthread_cond_signal(InitConds[pt]);
  }
  DDPrintf("T%d: pthread_self()=%p\n", INFO.tid, (void*)pt);
  UnsafeInitTidCommon();
  Tids[pt] = INFO.tid;
  PThreads[INFO.tid] = pt;
  ThreadInfoMap[pt] = &INFO;
}

INLINE tid_t GetTid() {
  return INFO.tid;
}

extern tid_t ExGetTid() {
  return GetTid();
}


// Flushes the local TLEB assuming someone is holding the global lock already.
// Our RTL shouldn't need a thread to flush someone else's TLEB.
void INLINE flush_tleb() {
// TODO(glider): we shouldn't need this anymore.
///    flush_trace();
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

void set_global_ignore(bool new_value) {
  GIL scoped;
  global_ignore = new_value;
  int add = new_value ? 1 : -1;
  map<pthread_t, ThreadInfo*>::iterator iter;
  for (iter = ThreadInfoMap.begin(); iter != ThreadInfoMap.end(); ++iter) {
    *(iter->second->thread_local_ignore) += add;
  }
}

void *pthread_callback(void *arg) {
  GIL::Lock();
  void *result = NULL;

  CHECK((PTH_INIT == 1) && (RTL_INIT == 1));
  CHECK(INIT == 0);
  InitTid();
  DECLARE_TID_AND_PC();
  DCHECK(INIT == 1);
  DCHECK(tid != 0);
  CHECK(INFO.tid != 0);

  memset(TLEB, '\0', sizeof(TLEB));

  callback_arg *cb_arg = (callback_arg*)arg;
  pthread_worker *routine = cb_arg->routine;
  void *routine_arg = cb_arg->arg;
  pthread_attr_t attr;
  size_t stack_size = 8 << 20;  // 8M
  void *stack_top = NULL;

  // We already know the child pid -- get the parent condvar to signal.
  tid_t parent = cb_arg->parent;
  CHECK(ChildThreadStartBarriers.find(parent) !=
         ChildThreadStartBarriers.end());
  pthread_barrier_t *parent_barrier = ChildThreadStartBarriers[parent];

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

  ShadowStack.end_ = ShadowStack.pcs_;
  SPut(THR_START, INFO.tid, (pc_t) &ShadowStack, 0, parent);
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
    SPut(THR_STACK_TOP, tid, pc, (uintptr_t)stack_top, stack_size);
//    SPut(MMAP, tid, pc, (uintptr_t)(stack_top) - stack_size, stack_size);
  } else {
    // Something's gone wrong. ThreadSanitizer will proceed, but if the stack
    // is reused by another thread, false positives will be reported.
    // &result is the address of a stack allocated var.
    SPut(THR_STACK_TOP, tid, pc, (uintptr_t)&result, stack_size);
  }
  unsafeMapTls(tid, pc);
  DDPrintf("Before routine() in T%d\n", tid);

  Finished[tid] = false;
#if (DEBUG)
  dump_finished();
#endif
  // Wait for the parent.
  __real_pthread_barrier_wait(parent_barrier);
  GIL::Unlock();

  result = (*routine)(routine_arg);

  // We're about to stop the current thread. Block all the signals to prevent
  // invoking the handlers after THR_END is sent to ThreadSanitizer.
  sigfillset(&glob_sig_blocked);
  pthread_sigmask(SIG_BLOCK, &glob_sig_blocked, &glob_sig_old);

  GIL::Lock();
  Finished[tid] = true;
#if (DEBUG)
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
    DDPrintf("T%d (child of T%d): Signaling on %p\n",
             tid, parent, FinishConds[tid]);
    __real_pthread_cond_signal(FinishConds[tid]);
  } else {
    DDPrintf("T%d (child of T%d): Not signaling, condvar not ready\n",
             tid, parent);
  }
  GIL::Unlock();
  return result;
}

// Erase the information about the deleted thread. This function should be
// always called after a lock.
void unsafe_forget_thread(tid_t tid, tid_t from) {
  DDPrintf("T%d: forgetting about T%d\n", from, tid);
  CHECK(PThreads.find(tid) != PThreads.end());
  pthread_t pt = PThreads[tid];
  Tids.erase(pt);
  PThreads.erase(tid);
  Finished.erase(tid);
  InitConds.erase(pt);
  FinishConds.erase(tid);
  ThreadInfoMap.erase(pt);
}

#define FLUSH_TRACE()
#if 0
#define FLUSH_TRACE() \
  flush_tleb()
#endif

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
void __wrap___libc_csu_init(void) {
  InitRTLAndTid0();
  __real___libc_csu_init();
}

extern "C"
int __wrap_pthread_create(pthread_t *thread,
                          pthread_attr_t *attr,
                          void *(*start_routine)(void*), void *arg) {
  FLUSH_TRACE();
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
  pthread_barrier_t *barrier;
  {
    GIL scoped;
    // |barrier| escapes to the child thread via ChildThreadStartBarriers.
    barrier = (pthread_barrier_t*) __real_malloc(sizeof(pthread_barrier_t));
    __real_pthread_barrier_init(barrier, NULL, 2);
    ChildThreadStartBarriers[tid] = barrier;
    DDPrintf("Setting ChildThreadStartBarriers[%d]\n", tid);
  }
  IN_RTL--;
  CHECK_IN_RTL();
  int result = __real_pthread_create(thread, attr, pthread_callback, cb_arg);
  tid_t child_tid = 0;
  if (result == 0) {
    __real_pthread_barrier_wait(barrier);
    GIL scoped;  // Should be strictly after pthread_barrier_wait()
    child_tid = Tids[*thread];
    ChildThreadStartBarriers.erase(tid);
    pthread_barrier_destroy(barrier);
    __real_free(barrier);
  } else {
    // Do not wait on the barrier.
    GIL scoped;
    child_tid = Tids[*thread];
    ChildThreadStartBarriers.erase(tid);
    pthread_barrier_destroy(barrier);
    __real_free(barrier);
  }
  if (!result) SPut(THR_CREATE_AFTER, tid, 0, 0, child_tid);
  DDPrintf("pthread_create(%p)\n", *thread);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}


INLINE void IGNORE_ALL_ACCESSES_BEGIN() {
  DECLARE_TID_AND_PC();
  Put(IGNORE_READS_BEG, tid, pc, 0, 0);
  Put(IGNORE_WRITES_BEG, tid, pc, 0, 0);
}

INLINE void IGNORE_ALL_ACCESSES_END() {
  DECLARE_TID_AND_PC();
  Put(IGNORE_READS_END, tid, pc, 0, 0);
  Put(IGNORE_WRITES_END, tid, pc, 0, 0);
}

INLINE void IGNORE_ALL_SYNC_BEGIN(void) {
  // TODO(glider): sync++
}

INLINE void IGNORE_ALL_SYNC_END(void) {
  // TODO(glider): sync--
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
  FLUSH_TRACE();
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
  FLUSH_TRACE();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap___cxa_guard_release, 0);
  long result = __real___cxa_guard_release(guard);
  IGNORE_ALL_ACCESSES_END();
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int __wrap_pthread_once(pthread_once_t *once_control,
                        void (*init_routine)(void)) {
  FLUSH_TRACE();
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
  if (IN_RTL) return __real_mmap(addr, length, prot, flags, fd, offset);
  GIL scoped;
  IN_RTL++;
  CHECK_IN_RTL();
  void *result;
  FLUSH_TRACE();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_mmap, 0);
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  result = __real_mmap(addr, length, prot, flags, fd, offset);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  if (result != (void*) -1) {
    SPut(MMAP, tid, pc, (uintptr_t)result, (uintptr_t)length);
  }
  RPut(RTN_EXIT, tid, pc, 0, 0);
  IN_RTL--;
  CHECK_IN_RTL();
  return result;
}

extern "C"
int __wrap_munmap(void *addr, size_t length) {
  if (IN_RTL) return __real_munmap(addr, length);
  GIL scoped;
  int result;
  IN_RTL++;
  CHECK_IN_RTL();
  FLUSH_TRACE();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_munmap, 0);
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  result = __real_munmap(addr, length);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  if (result == 0) {
    SPut(MUNMAP, tid, pc, (uintptr_t)addr, (uintptr_t)length);
  }
  RPut(RTN_EXIT, tid, pc, 0, 0);
  IN_RTL--;
  CHECK_IN_RTL();
  return result;
}

extern "C"
void *__wrap_calloc(size_t nmemb, size_t size) {
  void *result;
  FLUSH_TRACE();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_calloc, 0);
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  result = __real_calloc(nmemb, size);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  pc = (pc_t) __wrap_calloc;
  SPut(MALLOC, tid, pc, (uintptr_t)result, nmemb * size);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

// TODO(glider): we may want to eliminate the wrappers to weak functions that
// we replace (malloc(), free(), realloc()).
// TODO(glider): we may also want to handle calloc(), pvalloc() and other
// routines provided by libc.
// Wrap malloc() calls from the client code.
extern "C"
void *__wrap_malloc(size_t size) {
  if (IN_RTL) return __real_malloc(size);
  GIL scoped;
  FLUSH_TRACE();
  IN_RTL++;
  CHECK_IN_RTL();
  void *result;
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_malloc, 0);
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  result = __real_malloc(size);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  pc = (pc_t) __wrap_malloc;
  SPut(MALLOC, tid, pc, (uintptr_t)result, size);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  IN_RTL--;
  CHECK_IN_RTL();
  return result;
}

extern "C" void *__libc_malloc(size_t size);
extern "C" void __libc_free(void *ptr);
extern "C" void *__libc_realloc(void *ptr, size_t size);

// Wrap malloc() from libc.
extern "C"
void *malloc(size_t size) {
  if (IN_RTL || !RTL_INIT || !INIT) return __libc_malloc(size);
  GIL scoped;
  FLUSH_TRACE();
  IN_RTL++;
  CHECK_IN_RTL();
  void *result;
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)malloc, 0);
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  result = __libc_malloc(size);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  pc = (pc_t) malloc;
  SPut(MALLOC, tid, pc, (uintptr_t)result, size);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  IN_RTL--;
  CHECK_IN_RTL();
  return result;
}

extern "C"
void __wrap_free(void *ptr) {
  if (IN_RTL) return __real_free(ptr);
  GIL scoped;
  FLUSH_TRACE();
  IN_RTL++;
  CHECK_IN_RTL();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_free, 0);
  // Normally pc is equal to 0, but FREE asserts that it is not.
  SPut(FREE, tid, (pc_t)__wrap_free, (uintptr_t)ptr, 0);
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  __real_free(ptr);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  RPut(RTN_EXIT, tid, pc, 0, 0);
  IN_RTL--;
  CHECK_IN_RTL();
}

extern "C"
void free(void *ptr) {
  if (IN_RTL || !RTL_INIT || !INIT) return __libc_free(ptr);
  GIL scoped;
  FLUSH_TRACE();
  IN_RTL++;
  CHECK_IN_RTL();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)free, 0);
  // Normally pc is equal to 0, but FREE asserts that it is not.
  SPut(FREE, tid, (pc_t)free, (uintptr_t)ptr, 0);
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  __libc_free(ptr);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  RPut(RTN_EXIT, tid, pc, 0, 0);
  IN_RTL--;
  CHECK_IN_RTL();
}

extern "C"
void *__wrap_realloc(void *ptr, size_t size) {
  if (IN_RTL) return __real_realloc(ptr, size);
  GIL scoped;
  FLUSH_TRACE();
  void *result;
  IN_RTL++;
  CHECK_IN_RTL();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_realloc, 0);
  pc = (pc_t) __wrap_realloc;
  SPut(FREE, tid, pc, (uintptr_t)ptr, 0);
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  result = __real_realloc(ptr, size);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  SPut(MALLOC, tid, pc, (uintptr_t)result, size);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  IN_RTL--;
  CHECK_IN_RTL();
  return result;
}

extern "C"
void *realloc(void *ptr, size_t size) {
  if (IN_RTL || !RTL_INIT || !INIT) return __libc_realloc(ptr, size);
  GIL scoped;
  FLUSH_TRACE();
  void *result;
  IN_RTL++;
  CHECK_IN_RTL();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)realloc, 0);
  pc = (pc_t) realloc;
  SPut(FREE, tid, pc, (uintptr_t)ptr, 0);
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  result = __libc_realloc(ptr, size);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  SPut(MALLOC, tid, pc, (uintptr_t)result, size);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  IN_RTL--;
  CHECK_IN_RTL();
  return result;
}

// }}}

// new/delete {{{1
#ifdef TSAN_RTL_X86
extern "C"
void *__wrap__Znwj(unsigned int size) {
  if (IN_RTL) return __real__Znwj(size);
  GIL scoped;
  FLUSH_TRACE();
  IN_RTL++;
  CHECK_IN_RTL();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap__Znwj, 0);
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  void *result = __real__Znwj(size);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  pc = (pc_t) __wrap__Znwj;
  SPut(MALLOC, tid, pc, (uintptr_t)result, size);
  IN_RTL--;
  CHECK_IN_RTL();
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
void *__wrap__ZnwjRKSt9nothrow_t(unsigned long size, std::nothrow_t &nt) {
  if (IN_RTL) return __real__ZnwjRKSt9nothrow_t(size, nt);
  GIL scoped;
  FLUSH_TRACE();
  IN_RTL++;
  CHECK_IN_RTL();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap__ZnwjRKSt9nothrow_t, 0);
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  void *result = __real__ZnwjRKSt9nothrow_t(size, nt);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  pc = (pc_t) __wrap__ZnwjRKSt9nothrow_t;
  SPut(MALLOC, tid, pc, (uintptr_t)result, size);
  IN_RTL--;
  CHECK_IN_RTL();
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
void *__wrap__Znaj(unsigned int size) {
  if (IN_RTL) return __real__Znaj(size);
  GIL scoped;
  FLUSH_TRACE();
  IN_RTL++;
  CHECK_IN_RTL();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap__Znaj, 0);
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  void *result = __real__Znaj(size);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  pc = (pc_t) __wrap__Znaj;
  SPut(MALLOC, tid, pc, (uintptr_t)result, size);
  IN_RTL--;
  CHECK_IN_RTL();
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
void *__wrap__ZnajRKSt9nothrow_t(unsigned long size, std::nothrow_t &nt) {
  if (IN_RTL) return __real__ZnajRKSt9nothrow_t(size, nt);
  GIL scoped;
  FLUSH_TRACE();
  IN_RTL++;
  CHECK_IN_RTL();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap__ZnajRKSt9nothrow_t, 0);
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  void *result = __real__ZnajRKSt9nothrow_t(size, nt);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  pc = (pc_t) __wrap__ZnajRKSt9nothrow_t;
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
  GIL scoped;
  FLUSH_TRACE();
  IN_RTL++;
  CHECK_IN_RTL();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap__Znwm, 0);
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  void *result = __real__Znwm(size);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  pc = (pc_t) __wrap__Znwm;
  SPut(MALLOC, tid, pc, (uintptr_t)result, size);
  IN_RTL--;
  CHECK_IN_RTL();
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
void *__wrap__ZnwmRKSt9nothrow_t(unsigned long size, std::nothrow_t &nt) {
  if (IN_RTL) return __real__ZnwmRKSt9nothrow_t(size, nt);
  GIL scoped;
  FLUSH_TRACE();
  IN_RTL++;
  CHECK_IN_RTL();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap__ZnwmRKSt9nothrow_t, 0);
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  void *result = __real__ZnwmRKSt9nothrow_t(size, nt);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  pc = (pc_t) __wrap__ZnwmRKSt9nothrow_t;
  SPut(MALLOC, tid, pc, (uintptr_t)result, size);
  IN_RTL--;
  CHECK_IN_RTL();
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
void *__wrap__Znam(unsigned long size) {
  if (IN_RTL) return __real__Znam(size);
  GIL scoped;
  FLUSH_TRACE();
  IN_RTL++;
  CHECK_IN_RTL();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap__Znam, 0);
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  void *result = __real__Znam(size);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  pc = (pc_t) __wrap__Znam;
  SPut(MALLOC, tid, pc, (uintptr_t)result, size);
  IN_RTL--;
  CHECK_IN_RTL();
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
void *__wrap__ZnamRKSt9nothrow_t(unsigned long size, std::nothrow_t &nt) {
  if (IN_RTL) return __real__ZnamRKSt9nothrow_t(size, nt);
  GIL scoped;
  FLUSH_TRACE();
  IN_RTL++;
  CHECK_IN_RTL();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap__ZnamRKSt9nothrow_t, 0);
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  void *result = __real__ZnamRKSt9nothrow_t(size, nt);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  pc = (pc_t) __wrap__ZnamRKSt9nothrow_t;
  SPut(MALLOC, tid, pc, (uintptr_t)result, size);
  IN_RTL--;
  CHECK_IN_RTL();
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

#endif


extern "C"
void __wrap__ZdlPv(void *ptr) {
  if (IN_RTL) return __real__ZdlPv(ptr);
  GIL scoped;
  FLUSH_TRACE();
  IN_RTL++;
  CHECK_IN_RTL();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap__ZdlPv, 0);
  SPut(FREE, tid, (pc_t)__wrap__ZdlPv, (uintptr_t)ptr, 0);
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  //if (ptr) memset(ptr, 0, 4);
  __real__ZdlPv(ptr);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  IN_RTL--;
  CHECK_IN_RTL();
  RPut(RTN_EXIT, tid, pc, 0, 0);
}

extern "C"
void __wrap__ZdlPvRKSt9nothrow_t(void *ptr, std::nothrow_t &nt) {
  if (IN_RTL) return __real__ZdlPvRKSt9nothrow_t(ptr, nt);
  GIL scoped;
  FLUSH_TRACE();
  IN_RTL++;
  CHECK_IN_RTL();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap__ZdlPvRKSt9nothrow_t, 0);
  SPut(FREE, tid, (pc_t)__wrap__ZdlPvRKSt9nothrow_t, (uintptr_t)ptr, 0);
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  __real__ZdlPvRKSt9nothrow_t(ptr, nt);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  IN_RTL--;
  CHECK_IN_RTL();
  RPut(RTN_EXIT, tid, pc, 0, 0);
}

extern "C"
void __wrap__ZdaPv(void *ptr) {
  if (IN_RTL) return __real__ZdaPv(ptr);
  GIL scoped;
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

extern "C"
void __wrap__ZdaPvRKSt9nothrow_t(void *ptr, std::nothrow_t &nt) {
  if (IN_RTL) return __real__ZdaPvRKSt9nothrow_t(ptr, nt);
  GIL scoped;
  FLUSH_TRACE();
  IN_RTL++;
  CHECK_IN_RTL();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap__ZdaPvRKSt9nothrow_t, 0);
  SPut(FREE, tid, (pc_t)__wrap__ZdaPvRKSt9nothrow_t, (uintptr_t)ptr, 0);
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  __real__ZdaPvRKSt9nothrow_t(ptr, nt);
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
  FLUSH_TRACE();
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
  FLUSH_TRACE();
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
  FLUSH_TRACE();
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
  FLUSH_TRACE();
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
  FLUSH_TRACE();
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
  FLUSH_TRACE();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_pthread_cond_signal, 0);
  int result = __real_pthread_cond_signal(cond);
  SPut(SIGNAL, tid, pc, (uintptr_t)cond, 0);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int __wrap_pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
  FLUSH_TRACE();
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
  FLUSH_TRACE();
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
  FLUSH_TRACE();
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
  FLUSH_TRACE();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_pthread_mutex_unlock, 0);
  SPut(UNLOCK, tid, pc, (uintptr_t) mutex, 0);
  int result = __real_pthread_mutex_unlock(mutex);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int __wrap_pthread_mutex_destroy(pthread_mutex_t *mutex) {
  FLUSH_TRACE();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_pthread_mutex_destroy, 0);
  pc = (pc_t)__wrap_pthread_mutex_destroy;
  SPut(LOCK_DESTROY, tid, pc, (uintptr_t)mutex, 0);  // before the actual call.
  int result = __real_pthread_mutex_destroy(mutex);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int __wrap_pthread_mutex_init(pthread_mutex_t *mutex,
                              const pthread_mutexattr_t *attr) {
  FLUSH_TRACE();
  int result, mbRec;
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_pthread_mutex_init, 0);
  mbRec = 0;  // TODO(glider): unused so far.
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
  FLUSH_TRACE();
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
  FLUSH_TRACE();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_pthread_rwlock_destroy, 0);
  pc = (pc_t)__wrap_pthread_rwlock_destroy;
  SPut(LOCK_DESTROY, tid, pc, (uintptr_t)rwlock, 0);  // before the actual call.
  int result = __real_pthread_rwlock_destroy(rwlock);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int __wrap_pthread_rwlock_trywrlock(pthread_rwlock_t *rwlock) {
  FLUSH_TRACE();
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
  FLUSH_TRACE();
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
  FLUSH_TRACE();
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
  FLUSH_TRACE();
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
  FLUSH_TRACE();
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
  FLUSH_TRACE();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_pthread_barrier_init, 0);
  SPut(CYCLIC_BARRIER_INIT, tid, pc, (uintptr_t)barrier, count);
  int result = __real_pthread_barrier_init(barrier, attr, count);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int __wrap_pthread_barrier_wait(pthread_barrier_t *barrier) {
  FLUSH_TRACE();
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
                              void (*destr_function)(void *)) {
  FLUSH_TRACE();
  // We don't want libpthread to know about the destructors.
  int result = __real_pthread_key_create(key, NULL);
  if (destr_function && (result == 0)) {
    tsd_slot_index++;
    // TODO(glider): we should delete TSD slots on pthread_key_delete.
    DCHECK(tsd_slot_index < (int)(sizeof(tsd_slots) / sizeof(tsd_slot)));
    tsd_slots[tsd_slot_index].key = *key;
    tsd_slots[tsd_slot_index].dtor = destr_function;
  }
  return result;
}

extern "C"
int __wrap_pthread_join(pthread_t thread, void **value_ptr) {
  // Note that the ThreadInfo of |thread| is valid no more.
  FLUSH_TRACE();
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
      DDPrintf("T%d: Initializing InitConds[%p]=%p\n",
               tid, thread, InitConds[thread]);
      DDPrintf("T%d (parent of %p): Waiting on InitConds[%p]=%p\n",
               tid, thread, thread, InitConds[thread]);
      __real_pthread_cond_wait(InitConds[thread], &global_lock);
    }
    DCHECK(Tids.find(thread) != Tids.end());
    joined_tid = Tids[thread];
    DDPrintf("T%d: Finished[T%d]=%d\n", tid, joined_tid, Finished[joined_tid]);
    if (Finished.find(joined_tid) == Finished.end()) {
      Finished[joined_tid] = false;
      DDPrintf("T%d: setting Finished[T%d]=false\n", tid, joined_tid);
    }
    if (!Finished[joined_tid]) {
      FinishConds[joined_tid] = new pthread_cond_t;
      pthread_cond_init(FinishConds[joined_tid], NULL);
      DDPrintf("T%d: Initializing FinishConds[%d]=%p\n",
               tid, joined_tid, FinishConds[joined_tid]);
      DDPrintf("T%d (parent of T%d): Waiting on FinishConds[%d]=%p\n",
               tid, joined_tid, joined_tid, FinishConds[joined_tid]);
      __real_pthread_cond_wait(FinishConds[joined_tid], &global_lock);
    }
    unsafe_forget_thread(joined_tid, tid);  // TODO(glider): earlier?
    IN_RTL--;
    CHECK_IN_RTL();
  }

  int result = __real_pthread_join(thread, value_ptr);
  {
    DCHECK(joined_tid > 0);
    pc_t pc = GetPc();
    SPut(THR_JOIN_AFTER, tid, pc, joined_tid, 0);
  }
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int __wrap_pthread_spin_init(pthread_spinlock_t *lock, int pshared) {
  FLUSH_TRACE();
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
  FLUSH_TRACE();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_pthread_spin_destroy, 0);
  pc = (pc_t)__wrap_pthread_spin_destroy;
  SPut(LOCK_DESTROY, tid, pc, (uintptr_t)lock, 0);
  int result = __real_pthread_spin_destroy(lock);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}


extern "C"
int __wrap_pthread_spin_lock(pthread_spinlock_t *lock) {
  FLUSH_TRACE();
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
  FLUSH_TRACE();
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
  FLUSH_TRACE();
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
  FLUSH_TRACE();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_strchr, 0);
  pc = (pc_t)__wrap_strchr;
  char *result = Replace_strchr(tid, pc, s, c);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
char *__wrap_strrchr(const char *s, int c) {
  FLUSH_TRACE();
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
  FLUSH_TRACE();
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
  FLUSH_TRACE();
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
  FLUSH_TRACE();
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
  FLUSH_TRACE();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)strcpy, 0);
  pc = (pc_t)strcpy;
  char *result = Replace_strcpy(tid, pc, dest, src);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
void *__wrap_memchr(const char *s, int c, size_t n) {
  FLUSH_TRACE();
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
  FLUSH_TRACE();
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
  FLUSH_TRACE();
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
  FLUSH_TRACE();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_strcmp, 0);
  pc = (pc_t)__wrap_strcmp;
  int result = Replace_strcmp(tid, pc, s1, s2);
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

extern "C"
int strncmp(const char *s1, const char *s2, size_t n) {
  FLUSH_TRACE();
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
  CHECK(atexit_index < ATEXIT_MAX);
  atexit_stack[atexit_index] = worker;
}

atexit_worker* pop_atexit() {
  GIL scoped;
  CHECK(atexit_index > -1);
  return atexit_stack[atexit_index--];
}

void atexit_callback() {
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)atexit_callback, 0);
  atexit_worker *worker = pop_atexit();
  SPut(WAIT, tid, pc, (uintptr_t)worker, 0);
  (*worker)();
  RPut(RTN_EXIT, tid, pc, 0, 0);
}

extern "C"
int __wrap_atexit(void (*function)(void)) {
  FLUSH_TRACE();
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
  FLUSH_TRACE();
  if (IN_RTL) __real_exit(status);
  IN_RTL++;
  CHECK_IN_RTL();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_exit, 0);
  SPut(WAIT, tid, pc, kAtExitMagic, 0);
  __real_exit(status);
  // This in fact is never executed.
  IN_RTL--;
  CHECK_IN_RTL();
  RPut(RTN_EXIT, tid, pc, 0, 0);
}

// }}}

extern "C"
pid_t __wrap_fork() {
  FLUSH_TRACE();
  GIL scoped;
  pid_t result;
  IN_RTL++;
  CHECK_IN_RTL();
  Printf("Before fork() in process %d\n", getpid());
  result = __real_fork();
  Printf("After fork() in process %d\n", getpid());
  if (result == 0) {
    // Ignore all accesses in the child process. If someone is flushing the
    // TLEB in a thread under TSLock, and we're doing fork() in another thread,
    // then TSLock will remain locked forever in the child process, and trying
    // to analyze further memory accesses will cause a deadlock.
    // If someone is trying to take locks or spawn more threads in the child
    // process, he's very likely to have problems already -- let's not bother
    // him with race reports.

    // TODO(glider): Chrome does this often. Maybe it's better to re-initialize
    // ThreadSanitizer and some RTL parts upon fork().

    thread_local_ignore = 1;
    // Haha, all our resources that address the TLS of other threads are valid
    // no more!
    FORKED_CHILD = true;
    // TODO(glider): check for FORKED_CHILD every time we access someone's TLS.

    // We also keep IN_RTL > 0 to avoid other threads taking GIL for the same
    // reason.
    return result;
  }
  // Falling through to the parent process.
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
  if (S_TYPEISSHM(&stat_b)) result = fd;
  return result;
}

extern "C"
ssize_t __wrap_read(int fd, void *buf, size_t count) {
  FLUSH_TRACE();
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
  FLUSH_TRACE();
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

extern "C"
int __wrap_lockf64(int fd, int cmd, off_t len) {
  // TODO(glider): support len != 0
  if (IN_RTL || len) return __real_lockf64(fd, cmd, len);
  FLUSH_TRACE();
  IN_RTL++;
  CHECK_IN_RTL();
  DECLARE_TID_AND_PC();
  RPut(RTN_CALL, tid, pc, (uintptr_t)__wrap_lockf64, 0);
  int result = __real_lockf64(fd, cmd, len);
  if (result == 0) {
    if (cmd == F_LOCK) {
      SPut(WAIT, tid, pc, FdMagic(fd), 0);
    }
    if (cmd == F_ULOCK) {
      SPut(SIGNAL, tid, pc, FdMagic(fd), 0);
    }
  }
  IN_RTL--;
  CHECK_IN_RTL();
  RPut(RTN_EXIT, tid, pc, 0, 0);
  return result;
}

// Signal handling {{{1
/* Initial support for signals. Each user signal handler is stored in
 signal_actions[] and RTLSignalHandler is installed instead. When a signal is
 received, it is put into a thread-local array of pending signals (see the
 comments in RTLSignalHandler). Each time we're about to release the global
 lock, we handle all the pending signals.
*/
INLINE int unsafe_clear_pending_signals() {
  if (!have_pending_signals) return 0;
  int result = 0;
  for (int sig = 0; sig < NSIG; sig++) {
    if (pending_signal_flags[sig]) {
      DDPrintf("[T%d] Pending signal: %d\n", GetTid(), sig);
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
  *ShadowStack.end_ = (uintptr_t)addr;
  ShadowStack.end_++;
  DCHECK(ShadowStack.end_ > ShadowStack.pcs_);
  DCHECK((size_t)(ShadowStack.end_ - ShadowStack.pcs_) < kMaxCallStackSize);
}

extern "C"
void rtn_exit() {
  DCHECK(ShadowStack.end_ > ShadowStack.pcs_);
  DCHECK((size_t)(ShadowStack.end_ - ShadowStack.pcs_) < kMaxCallStackSize);
  ShadowStack.end_--;
}

// TODO(glider): bb_flush is deprecated.
#if 0
// TODO(glider): we may want the basic block address to differ from the PC
// of the first MOP in that basic block.
extern "C"
void bb_flush(TraceInfoPOD *next_mops) {
  DCHECK(INIT);
  if (INFO.trace_info) {
    // This is not a function entry block
    flush_trace();
  }
  INFO.trace_info = next_mops;
}
#endif

extern "C"
void bb_flush_current(TraceInfoPOD *curr_mops) {
  flush_trace(curr_mops);
}

extern "C"
void *rtl_memcpy(char *dest, const char *src, size_t n) {
  FLUSH_TRACE();
  DECLARE_TID_AND_PC();
  pc = (pc_t)rtl_memcpy;
  return Replace_memcpy(tid, pc, dest, src, n);
}

extern "C"
void *rtl_memmove(char *dest, const char *src, size_t n) {
  FLUSH_TRACE();
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
      // If we've already seen this pc, check that it is inside the same
      // function, but do not update the debug info (it may be different).
      // TODO(glider): generate more correct debug info.
      if ((*debug_info).find(pcs[i].pc) != (*debug_info).end()) {
        if ((*debug_info)[pcs[i].pc].symbol != symbols[pcs[i].symbol]) {
          Printf("ERROR: conflicting symbols for pc %p:\n  %s\n  %s\n",
                 pcs[i].pc,
                 (*debug_info)[pcs[i].pc].symbol.c_str(),
                 symbols[pcs[i].symbol].c_str());
///          CHECK(false);
        }
      } else {
        (*debug_info)[pcs[i].pc].pc = pcs[i].pc;
        (*debug_info)[pcs[i].pc].symbol = symbols[pcs[i].symbol];
#if defined(__GNUC__)
        char *demangled = NULL;
        int status;
        demangled = __cxxabiv1::__cxa_demangle(symbols[pcs[i].symbol].c_str(),
                                               0, 0, &status);
        if (demangled) {
          (*debug_info)[pcs[i].pc].demangled_symbol = demangled;
          __real_free(demangled);
        } else {
          (*debug_info)[pcs[i].pc].demangled_symbol = symbols[pcs[i].symbol];
        }
#else
        (*debug_info)[pcs[i].pc].demangled_symbol = symbols[pcs[i].symbol];
#endif
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
}

void AddOneWrapperDbgInfo(pc_t pc, const char *symbol) {
  (*debug_info)[pc].pc = pc;
  (*debug_info)[pc].symbol = symbol;
  (*debug_info)[pc].demangled_symbol = symbol;
  (*debug_info)[pc].fullpath = __FILE__;
  (*debug_info)[pc].file = __FILE__;
  (*debug_info)[pc].path = "";
  // TODO(glider): we need exact line numbers.
  (*debug_info)[pc].line = 0;
}

#define WRAPPER_DBG_INFO(fun) AddOneWrapperDbgInfo((pc_t)fun, #fun)

void AddWrappersDbgInfo() {
  WRAPPER_DBG_INFO(__wrap_pthread_create);
  WRAPPER_DBG_INFO(__wrap_pthread_join);
  WRAPPER_DBG_INFO(__wrap_pthread_mutex_init);
  WRAPPER_DBG_INFO(__wrap_pthread_mutex_lock);
  WRAPPER_DBG_INFO(__wrap_pthread_mutex_trylock);
  WRAPPER_DBG_INFO(__wrap_pthread_mutex_unlock);
  WRAPPER_DBG_INFO(__wrap_pthread_mutex_destroy);

  WRAPPER_DBG_INFO(__wrap_pthread_rwlock_init);
  WRAPPER_DBG_INFO(__wrap_pthread_rwlock_destroy);
  WRAPPER_DBG_INFO(__wrap_pthread_rwlock_trywrlock);
  WRAPPER_DBG_INFO(__wrap_pthread_rwlock_wrlock);
  WRAPPER_DBG_INFO(__wrap_pthread_rwlock_tryrdlock);
  WRAPPER_DBG_INFO(__wrap_pthread_rwlock_rdlock);
  WRAPPER_DBG_INFO(__wrap_pthread_rwlock_unlock);

  WRAPPER_DBG_INFO(__wrap_pthread_spin_init);
  WRAPPER_DBG_INFO(__wrap_pthread_spin_destroy);
  WRAPPER_DBG_INFO(__wrap_pthread_spin_lock);
  WRAPPER_DBG_INFO(__wrap_pthread_spin_trylock);
  WRAPPER_DBG_INFO(__wrap_pthread_spin_unlock);

  WRAPPER_DBG_INFO(__wrap_pthread_cond_signal);
  WRAPPER_DBG_INFO(__wrap_pthread_cond_wait);
  WRAPPER_DBG_INFO(__wrap_pthread_cond_timedwait);

  WRAPPER_DBG_INFO(__wrap_pthread_key_create);

  WRAPPER_DBG_INFO(__wrap_sem_open);
  WRAPPER_DBG_INFO(__wrap_sem_wait);
  WRAPPER_DBG_INFO(__wrap_sem_trywait);
  WRAPPER_DBG_INFO(__wrap_sem_post);

  WRAPPER_DBG_INFO(__wrap___cxa_guard_acquire);
  WRAPPER_DBG_INFO(__wrap___cxa_guard_release);

  WRAPPER_DBG_INFO(__wrap_atexit);
  WRAPPER_DBG_INFO(__wrap_exit);

  WRAPPER_DBG_INFO(__wrap_strlen);
  WRAPPER_DBG_INFO(__wrap_strcmp);
  WRAPPER_DBG_INFO(__wrap_memchr);
  WRAPPER_DBG_INFO(__wrap_memcpy);
  WRAPPER_DBG_INFO(__wrap_memmove);
  WRAPPER_DBG_INFO(__wrap_strchr);
  WRAPPER_DBG_INFO(__wrap_strrchr);

  WRAPPER_DBG_INFO(__wrap_mmap);
  WRAPPER_DBG_INFO(__wrap_munmap);
  WRAPPER_DBG_INFO(__wrap_calloc);
  WRAPPER_DBG_INFO(__wrap_malloc);
  WRAPPER_DBG_INFO(__wrap_realloc);
  WRAPPER_DBG_INFO(__wrap_free);

  WRAPPER_DBG_INFO(malloc);
  WRAPPER_DBG_INFO(free);
  WRAPPER_DBG_INFO(realloc);

  WRAPPER_DBG_INFO(__wrap_read);
  WRAPPER_DBG_INFO(__wrap_write);

  WRAPPER_DBG_INFO(__wrap_pthread_once);
  WRAPPER_DBG_INFO(__wrap_pthread_barrier_init);
  WRAPPER_DBG_INFO(__wrap_pthread_barrier_wait);

  WRAPPER_DBG_INFO(__wrap_sigaction);

#ifdef TSAN_RTL_X86
  WRAPPER_DBG_INFO(__wrap__Znwj);
  WRAPPER_DBG_INFO(__wrap__ZnwjRKSt9nothrow_t);
  WRAPPER_DBG_INFO(__wrap__Znaj);
  WRAPPER_DBG_INFO(__wrap__ZnajRKSt9nothrow_t);
#endif  // TSAN_RTL_X86
#ifdef TSAN_RTL_X64
  WRAPPER_DBG_INFO(__wrap__Znwm);
  WRAPPER_DBG_INFO(__wrap__ZnwmRKSt9nothrow_t);
  WRAPPER_DBG_INFO(__wrap__Znam);
  WRAPPER_DBG_INFO(__wrap__ZnamRKSt9nothrow_t);
#endif  // TSAN_RTL_X64
  WRAPPER_DBG_INFO(__wrap__ZdlPv);
  WRAPPER_DBG_INFO(__wrap__ZdlPvRKSt9nothrow_t);
  WRAPPER_DBG_INFO(__wrap__ZdaPv);
  WRAPPER_DBG_INFO(__wrap__ZdaPvRKSt9nothrow_t);

  WRAPPER_DBG_INFO(atexit_callback);
  WRAPPER_DBG_INFO(strcpy);
  WRAPPER_DBG_INFO(strncmp);
}

void ReadElf() {
  const int kBufSize = 1000;
  char fname[kBufSize];
  memset(fname, '\0', sizeof(fname));
  int fsize = readlink("/proc/self/exe", fname, kBufSize);
  CHECK(fsize < kBufSize);
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
  // As per csu/libc-tls.c, static TLS block has some surplus bytes beyond the
  // size of .tdata and .tbss.
  static_tls_size = 2048;

  IN_RTL++;
  CHECK_IN_RTL();
  for (int i = 0; i < shnum; ++i) {
    Elf_Shdr* shdr = shdrs + i;
    Elf_Off off = shdr->sh_offset;
    Elf_Word name = shdr->sh_name;
    Elf_Word size = shdr->sh_size;
    Elf_Word flags = shdr->sh_flags;
    DDPrintf("Section name: %d, %s\n", name, hdr_strings + name);
    if (strcmp(hdr_strings + name, "tsan_rtl_debug_info") == 0) {
      debug_info_section = map + off;
      debug_info_size = size;
      continue;
    }
    if (flags && SHF_TLS) {
      if ((strcmp(hdr_strings + name, ".tbss") == 0) ||
          (strcmp(hdr_strings + name, ".tdata") == 0)) {
        static_tls_size += size;
        continue;
      }
    }
  }
  IN_RTL--;
  CHECK_IN_RTL();
  CHECK(debug_info_section);
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
    info.pc = strtol(parts[0].c_str(), NULL, 16);  // TODO(glider): error code
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
    if (demangle) {
      *rtn_name = (*debug_info)[pc].demangled_symbol;
    } else {
      *rtn_name = (*debug_info)[pc].symbol;
    }
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
