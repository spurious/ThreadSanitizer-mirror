#include "tsan_rtl.h"
#include <sys/stat.h>


static inline void Put(EventType type, int32_t tid, pc_t pc,
                       uintptr_t a, uintptr_t info);


#define EXTRA_REPLACE_PARAMS int tid, pc_t pc,
#define REPORT_READ_RANGE(x, size) do { \
    if (size) Put(READ, tid, pc, (uintptr_t)(x), (size)); } while(0)
#define REPORT_WRITE_RANGE(x, size) do { \
    if (size) Put(WRITE, tid, pc, (uintptr_t)(x), (size)); } while(0)
#include "ts_replace.h"

#define DEBUG 1
#undef DEBUG

#ifdef DEBUG
# define DPrintf(params...) \
    Printf(params)
# define DEBUG_DO(code) \
  do { code } while(0)
#else
# define DPrintf(params...)
# define DEBUG_DO(code)
#endif


struct LLVMMopInfo {
  pc_t pc;  // in fact it's the MOp number
  int size;
  int is_write;
};

struct Passport {
  LLVMMopInfo *mop_info;
  int num_mops;
  int flushed;
  void *curr_bb_index;
  void *known_bb_addr;
};

struct ThreadInfo {
  int tid;
  uintptr_t TLEB[1000];
  Passport passport[100];
  int passport_index;
};

struct LLVMDebugInfo {
  pc_t pc;
  int line;
  string fun;
  string file;
  string path;
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
__thread int events = 0;

FILE *out_file = NULL;
int RTL_INIT = 0;
int PTH_INIT = 0;
int DBG_INIT = 0;
int HAVE_THREAD_0 = 0;
static bool global_ignore = true;

pthread_mutex_t global_lock = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
pthread_t gil_owner = 0;
int gil_depth = 0;
std::map<pthread_t, int> Tids;
std::map<int, pthread_t> PThreads;
std::map<int, bool> Finished;
std::map<int, pthread_cond_t*> FinishConds; // TODO(glider): we shouldn't need these.
std::map<pthread_t, pthread_cond_t*> InitConds; // TODO(glider): we shouldn't need these.
int max_tid = -1;
//__thread int TID = -1;
//__thread uintptr_t TLEB[1000];
//__thread uintptr_t curr_bb_index = 0;

class GIL {
 public:
  GIL() {
    Lock();
  }
  ~GIL() {
    Unlock();
  }
  static void Lock() {
    __real_pthread_mutex_lock(&global_lock);
#ifdef DEBUG
    gil_owner = pthread_self();
    gil_depth++;
#endif

  }
  static void Unlock() {
#ifdef DEBUG
    gil_depth--;
    if (gil_depth == 0) gil_owner = 0;
#endif

    __real_pthread_mutex_unlock(&global_lock);
  }
#ifdef DEBUG
  static pthread_t GetOwner() {
    return gil_owner;
  }
#endif
};

static inline void UPut(EventType type, int32_t tid, pc_t pc,
                        uintptr_t a, uintptr_t info) {
#ifdef DEBUG
  assert(GIL::GetOwner() == pthread_self());
#endif
  if (!HAVE_THREAD_0 && type != THR_START) {
    UPut(THR_START, 0, 0, 0, 0);
  }
  if (RTL_INIT != 1) return;
  if (!global_ignore ||
      ((type!=READ) && (type!=WRITE) && (type!=SBLOCK_ENTER) &&
       (type != RTN_CALL) && (type != RTN_EXIT))) {
    Event event(type, tid, pc, a, info);
    if (G_flags->verbosity) {
      if ((G_flags->verbosity >= 2) ||
          (type == THR_START) || (type == THR_END) || (type == THR_JOIN_AFTER) || (type == THR_CREATE_BEFORE)) {
        event.Print();
      }
    }
    ThreadSanitizerHandleOneEvent(&event);
    if ((type==THR_START) && (tid==0)) HAVE_THREAD_0 = 1;
  }
}

static inline void Put(EventType type, int32_t tid, pc_t pc,
                       uintptr_t a, uintptr_t info) {
  GIL scoped;
  UPut(type, tid, pc, a, info);
}

// TODO(glider): atexit()
void finalize() {
  ThreadSanitizerFini();
  fclose(out_file);
  out_file = NULL;
}
inline void init_debug() {
  if (DBG_INIT) return;
  char *dbg_info = getenv("TSAN_DBG_INFO");
  if (dbg_info) {
    ReadDbgInfo(dbg_info);
  }
  DBG_INIT = 1;
}

bool in_initialize = false;

bool initialize() {
  if (in_initialize) return false;
  in_initialize = true;
  // Only one thread exists at this moment.
  G_flags = new FLAGS;
  out_file = fopen("offline.events", "w");
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

  ThreadSanitizerParseFlags(&args);
  ThreadSanitizerInit();
  init_debug();
  global_ignore = false;
  __real_atexit(finalize);
  RTL_INIT = 1;
  in_initialize = false;
  return true;
}

class Init {
 public:
  Init() {
    initialize();
  }
};

static Init dummy_init;

pc_t GetPc() {
  return 0;
}

int GetTid(ThreadInfo *info) {
  GIL scoped;
  //if (!PTH_INIT && RTL_INIT) return 0;
  if (INIT == 0) {
    // thread initialization
    pthread_t pt = pthread_self();
    max_tid++;
    info->tid = max_tid;
    info->passport_index = 0;
    info->passport[0].mop_info = NULL;
    info->passport[0].num_mops = 0;
    info->passport[0].flushed = 0;
    info->passport[0].curr_bb_index = NULL;
    info->passport[0].known_bb_addr = NULL;
    switch (RTL_INIT) {
      case 1: {
        Tids[pt] = info->tid;
        if (InitConds.find(pt) != InitConds.end()) {
          __real_pthread_cond_signal(InitConds[pt]);
        }
        DPrintf("T%d: pthread_self()=%p\n", info->tid, (void*)pt);
        PThreads[info->tid] = pt;
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
                 __real_exit(2);
               }
    }
    if (info->tid == 0) {
      //UPut(THR_START, 0, 0, 0, 0);
    }
    INIT = 1;
  }
  return info->tid;
}


int GetTid() {
  return GetTid(&INFO);
}

void unsafe_flush_tleb_slice(ThreadInfo *info,
                             int slice_start, int slice_end) {
  // {{{1
  Passport &passport = info->passport[info->passport_index];
  int tid = info->tid;
  if (slice_start == 0) {
    // enter sblock
    void *bb_addr = passport.curr_bb_index;
    if (bb_addr) {
      UPut(SBLOCK_ENTER, (uintptr_t)tid, (uintptr_t)bb_addr, 0, 0);
    }
  }
  LLVMMopInfo *mops = passport.mop_info;
  int read_len = 1, write_len = 1;
  for (int i = slice_start; i < slice_end; ++i) {
    unsigned long addr = info->TLEB[i];
    switch (mops[i].is_write) {
      case 1: {
        UPut(WRITE, tid, mops[i].pc, addr, mops[i].size * write_len);
        write_len = 1;
        break;
      }
      case 0: {
        UPut(READ, tid, mops[i].pc, addr, mops[i].size * read_len);
        read_len = 1;
        break;
      }
      case 2: {
        write_len = addr;
        read_len = addr;
        break;
      }
    }
  }
  passport.flushed = slice_end;
  if (slice_end == passport.num_mops) {
    //fprintf(out_file, "#===BB_END=====\n\n");
  }
} // }}}



// TODO(glider): we may want the basic block address to differ from the PC
// of the first MOP in that basic block.
extern "C"
void* bb_flush(LLVMMopInfo *next_mops,
               int next_num_mops, void *next_bb_index) {
  // {{{1
  Passport &passport = INFO.passport[INFO.passport_index];
  if (passport.mop_info) {
    // This is not a function entry block
    GIL scoped;
    unsafe_flush_tleb_slice(&INFO, passport.flushed, passport.num_mops);
    passport.flushed = 0;
  }
  passport.mop_info = next_mops;
  passport.num_mops = next_num_mops;
  if (passport.known_bb_addr) {
    passport.curr_bb_index = passport.known_bb_addr;
    passport.known_bb_addr = NULL;
  } else {
    passport.curr_bb_index = next_bb_index;
  }
  return (void*) INFO.TLEB;
} // }}}

extern "C"
void bb_flush_slice(int slice_start, int slice_end) {
  GIL scoped;
  unsafe_flush_tleb_slice(&INFO, slice_start, slice_end);
}

// Flushes the local TLEB assuming someone is holding the global lock already.
// Our RTL shouldn't need a thread to flush someone else's TLEB.
void unsafe_flush_tleb() {
  ThreadInfo *info = &INFO;
  if (info) {
    Passport &passport = info->passport[info->passport_index];
    if (passport.num_mops) {
      unsafe_flush_tleb_slice(info, passport.flushed, passport.num_mops);
      passport.flushed = 0;
      passport.num_mops = 0;
    }
  } else {
    DPrintf("ERROR: thread %d not found!\n", tid);
    assert(0);
  }
  Passport &passport = info->passport[info->passport_index];
  if (passport.known_bb_addr) {
    passport.curr_bb_index = passport.known_bb_addr;
    passport.known_bb_addr = NULL;
  } else {
    passport.curr_bb_index = 0;
  }

}

void unused_unsafe_flush_tleb(int tid) {
  ThreadInfo *info = NULL;
  int local_tid = GetTid();
  if (local_tid == tid) {
    info = &INFO;
  } else if (PThreads.find(tid) != PThreads.end()) {
    DPrintf("T%d: pthread_self()=%p, info=%p, info->tid: %d\n", local_tid, (void*)PThreads[tid], info, info->tid);
  }
  if (info) {
    Passport &passport = info->passport[info->passport_index];
    if (passport.num_mops) {
      unsafe_flush_tleb_slice(info, passport.flushed, passport.num_mops);
      passport.flushed = 0;
      passport.num_mops = 0;
    }
  } else {
    //fprintf(out_file, "#ERROR: thread %d not found!\n", tid);
  }
  Passport &passport = info->passport[info->passport_index];
  if (passport.known_bb_addr) {
    passport.curr_bb_index = passport.known_bb_addr;
    passport.known_bb_addr = NULL;
  } else {
    passport.curr_bb_index = 0;
  }

}


typedef void *(pthread_worker)(void*);

struct callback_arg {
  pthread_worker *routine;
  void *arg;
  int parent;
  pthread_attr_t *attr;
};

// TODO(glider): we should get rid of Finished[],
// as FinishConds should guarantee that the thread has finished.
void dump_finished() {
  map<int, bool>::iterator iter;
  for (iter = Finished.begin(); iter != Finished.end(); ++iter) {
    DPrintf("Finished[%d] = %d\n", iter->first, iter->second);
  }
}

void *pthread_callback(void *arg) {
  void *result = NULL;

  assert((PTH_INIT==1) && (RTL_INIT==1));
  assert(INIT == 0);
  int tid = GetTid();
  pc_t pc = GetPc();
  assert(INIT == 1);
  assert(tid != 0);
  assert(INFO.tid != 0);

  callback_arg *cb_arg = (callback_arg*)arg;
  pthread_worker *routine = cb_arg->routine;
  void *routine_arg = cb_arg->arg;
  int parent = cb_arg->parent;
  pthread_attr_t *attr = cb_arg->attr;
  // TODO(glider): we may depend on ulimit().
  size_t stack_size = 8 << 20;  // 8M
  if (attr) {
    pthread_attr_getstacksize(attr, &stack_size);
  }


  Put(THR_START, INFO.tid, 0, 0, parent);
  delete cb_arg;
  Put(THR_STACK_TOP, tid, pc, (uintptr_t)&result, stack_size);
  DPrintf("Before routine() in T%d\n", tid);

  GIL::Lock();
  Finished[tid] = false;
  dump_finished();
  GIL::Unlock();

  result = (*routine)(routine_arg);
  GIL::Lock();
  Finished[tid] = true;
  dump_finished();
  DPrintf("After routine() in T%d\n", tid);

  // Flush all the events not flushed so far.
  unsafe_flush_tleb();
  UPut(THR_END, tid, 0, 0, 0);
  if (FinishConds.find(tid) != FinishConds.end()) {
    DPrintf("T%d (child of T%d): Signaling on %p\n", tid, parent, FinishConds[tid]);
    __real_pthread_cond_signal(FinishConds[tid]);
  } else {
    DPrintf("T%d (child of T%d): Not signaling, condvar not ready\n", tid, parent);
  }
  GIL::Unlock();
  return result;
}

void unsafe_forget_thread(int tid, int from) {
  DPrintf("T%d: forgetting about T%d\n", from, tid);
  assert(PThreads.find(tid) != PThreads.end());
  pthread_t pt = PThreads[tid];
  Tids.erase(pt);
  PThreads.erase(tid);
  Finished.erase(tid);
  InitConds.erase(pt);
  FinishConds.erase(tid);
}

extern "C"
int __wrap_pthread_create(pthread_t *thread,
                          pthread_attr_t *attr,
                          void *(*start_routine)(void*), void *arg) {
  int tid = GetTid();
  callback_arg *cb_arg = new callback_arg;
  cb_arg->routine = start_routine;
  cb_arg->arg = arg;
  cb_arg->parent = tid;
  cb_arg->attr = attr;
  Put(THR_CREATE_BEFORE, tid, 0, 0, 0);
  PTH_INIT = 1;
  int result = __real_pthread_create(thread, attr, pthread_callback, cb_arg);
  DPrintf("pthread_create(%p)\n", *thread);
  return result;
}


static inline void IGNORE_ALL_ACCESSES_BEGIN() {
  DECLARE_TID_AND_PC();
  Put(IGNORE_READS_BEG, tid, pc, 0, 0);
  Put(IGNORE_WRITES_BEG, tid, pc, 0, 0);
}

static inline void IGNORE_ALL_ACCESSES_END() {
  DECLARE_TID_AND_PC();
  Put(IGNORE_READS_END, tid, pc, 0, 0);
  Put(IGNORE_WRITES_END, tid, pc, 0, 0);
}

static inline void IGNORE_ALL_SYNC_BEGIN(void) {
  //TODO(glider): sync++
}

static inline void IGNORE_ALL_SYNC_END(void) {
  //TODO(glider): sync--
}


static inline void IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN(void) {
  IGNORE_ALL_ACCESSES_BEGIN();
  IGNORE_ALL_SYNC_BEGIN();
}

static inline void IGNORE_ALL_ACCESSES_AND_SYNC_END(void) {
  IGNORE_ALL_ACCESSES_END();
  IGNORE_ALL_SYNC_END();
}

// TODO(glider): for each runtime static initialization a guard variable
// with a name like "_ZGVZN4testEvE1a" ("guard variable for test()::a")
// is created. It's more correct to emit a WAIT event upon each read of
// such a variable, and a SIGNAL upon each write (or the corresponding
// __cxa_guard_release() call.
extern "C"
int __wrap___cxa_guard_acquire(int *guard) {
  long result = __real___cxa_guard_acquire(guard);
  IGNORE_ALL_ACCESSES_BEGIN();
  if (!result) {
    IGNORE_ALL_ACCESSES_END();
  }
  return result;
}

extern "C"
int __wrap___cxa_guard_release(int *guard) {
  long result = __real___cxa_guard_release(guard);
  IGNORE_ALL_ACCESSES_END();
  return result;
}

extern "C"
int __wrap_pthread_once(pthread_once_t *once_control,
                        void (*init_routine) (void)) {
  IGNORE_ALL_ACCESSES_BEGIN();
  int result = __real_pthread_once(once_control, init_routine);
  IGNORE_ALL_ACCESSES_END();
  return result;
}

// Memory allocation routines {{{1
extern "C"
void *__wrap_mmap(void *addr, size_t length, int prot, int flags,
                  int fd, off_t offset) {
  void *result;
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  result = __real_mmap(addr, length, prot, flags, fd, offset);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  if (result != (void*) -1) {
    DECLARE_TID_AND_PC();
    Put(MMAP, tid, pc, (uintptr_t)result, (uintptr_t)length);
  }
  return result;
}

extern "C"
int __wrap_munmap(void *addr, size_t length) {
  int result;
  DECLARE_TID_AND_PC();
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  result = __real_munmap(addr, length);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  if (result == 0) {
    Put(MUNMAP, tid, pc, (uintptr_t)addr, (uintptr_t)length);
  }
  return result;
}

extern "C"
void *__wrap_calloc(size_t nmemb, size_t size) {
  void *result;
  DECLARE_TID_AND_PC();
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  result = __real_calloc(nmemb, size);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  Put(MALLOC, tid, pc, (uintptr_t)result, nmemb * size);
  return result;
}

extern "C"
void *__wrap_malloc(size_t size) {
  void *result;
  DECLARE_TID_AND_PC();
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  result = __real_malloc(size);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  Put(MALLOC, tid, pc, (uintptr_t)result, size);
  return result;
}

extern "C"
void __wrap_free(void *ptr) {
  DECLARE_TID_AND_PC();
  Put(FREE, tid, pc, (uintptr_t)ptr, 0);
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  __real_free(ptr);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
}

extern "C"
void *__wrap_realloc(void *ptr, size_t size) {
  void *result;
  DECLARE_TID_AND_PC();
  IGNORE_ALL_ACCESSES_AND_SYNC_BEGIN();
  result = __real_realloc(ptr, size);
  IGNORE_ALL_ACCESSES_AND_SYNC_END();
  Put(MALLOC, tid, pc, (uintptr_t)result, size);
  return result;
}

// }}}

// Unnamed POSIX semaphores {{{1
// TODO(glider): support AnnotateIgnoreSync here.

extern "C"
sem_t *__wrap_sem_open(const char *name, int oflag,
                mode_t mode, unsigned int value) {
  sem_t *result = __real_sem_open(name, oflag, mode, value);
  if ((oflag & O_CREAT) &&
      value > 0 &&
      result != SEM_FAILED) {
    DECLARE_TID_AND_PC();
    Put(SIGNAL, tid, pc, (uintptr_t)result, 0);
  }
  return result;
}

extern "C"
int __wrap_sem_wait(sem_t *sem) {
  DECLARE_TID_AND_PC();
  int result = __real_sem_wait(sem);
  if (result == 0) {
    Put(WAIT, tid, pc, (uintptr_t)sem, 0);
  }
  return result;
}

extern "C"
int __wrap_sem_trywait(sem_t *sem) {
  DECLARE_TID_AND_PC();
  int result = __real_sem_trywait(sem);
  if (result == 0) {
    Put(WAIT, tid, pc, (uintptr_t)sem, 0);
  }
  return result;
}

extern "C"
int __wrap_sem_post(sem_t *sem) {
  DECLARE_TID_AND_PC();
  Put(SIGNAL, tid, pc, (uintptr_t)sem, 0);
  return __real_sem_post(sem);
}

// }}}

// libpthread wrappers {{{1
extern "C"
int __wrap_pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                                  const struct timespec *abstime) {
  int tid = GetTid();
  pc_t pc = GetPc();
  Put(UNLOCK, tid, pc, (uintptr_t)mutex, 0);
  int result = __real_pthread_cond_timedwait(cond, mutex, abstime);
  if (result == 0) {
    Put(WAIT, tid, pc, (uintptr_t)cond, 0);
  }
  Put(WRITER_LOCK, tid, pc, (uintptr_t)mutex, 0);
  return result;
}

extern "C"
int __wrap_pthread_cond_signal(pthread_cond_t *cond) {
  int tid = GetTid();
  pc_t pc = GetPc();
  int result = __real_pthread_cond_signal(cond);
  Put(SIGNAL, tid, pc, (uintptr_t)cond, 0);
  return result;
}

extern "C"
int __wrap_pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
  DECLARE_TID_AND_PC();
  Put(UNLOCK, tid, pc, (uintptr_t)mutex, 0);
  int result = __real_pthread_cond_wait(cond, mutex);
  Put(WAIT, tid, pc, (uintptr_t)cond, 0);
  Put(WRITER_LOCK, tid, pc, (uintptr_t)mutex, 0);
  return result;
}

extern "C"
int __wrap_pthread_mutex_lock(pthread_mutex_t *mutex) {
  int result = __real_pthread_mutex_lock(mutex);
  if (result == 0 /* success */) {
    DECLARE_TID_AND_PC();
    Put(WRITER_LOCK, tid, pc, (uintptr_t)mutex, 0);
  }
  // TODO(glider): should we handle error codes?
  return result;
}

extern "C"
int __wrap_pthread_mutex_trylock(pthread_mutex_t *mutex) {
  int result = __real_pthread_mutex_trylock(mutex);
  if (result == 0) {
    DECLARE_TID_AND_PC();
    Put(WRITER_LOCK, tid, pc, (uintptr_t)mutex, 0);
  }
  return result;
}

extern "C"
int __wrap_pthread_mutex_unlock(pthread_mutex_t *mutex) {
  DECLARE_TID_AND_PC();
  Put(UNLOCK, tid, pc, (uintptr_t) mutex, 0);
  return __real_pthread_mutex_unlock(mutex);
}

extern "C"
int __wrap_pthread_mutex_destroy(pthread_mutex_t *mutex) {
  DECLARE_TID_AND_PC();
  Put(LOCK_DESTROY, tid, pc, (uintptr_t)mutex, 0);  // before the actual call.
  return __real_pthread_mutex_destroy(mutex);
}

extern "C"
int __wrap_pthread_mutex_init(pthread_mutex_t *mutex,
                              const pthread_mutexattr_t *attr) {
  int result, mbRec;
  DECLARE_TID_AND_PC();
  mbRec = 0; // unused so far
  if (attr) {
    int ty, zzz;
    zzz = pthread_mutexattr_gettype(attr, &ty);
    if (zzz == 0 && ty == PTHREAD_MUTEX_RECURSIVE) mbRec = 1;
  }
  result = __real_pthread_mutex_init(mutex, attr);
  if (result == 0) {
    Put(LOCK_CREATE, tid, pc, (uintptr_t)mutex, 0);
  }
  return result;
}

extern "C"
int __wrap_pthread_rwlock_init(pthread_rwlock_t *rwlock,
                               const pthread_rwlockattr_t *attr) {
  DECLARE_TID_AND_PC();
  int result = __real_pthread_rwlock_init(rwlock, attr);
  if (result == 0) {
    Put(LOCK_CREATE, tid, pc, (uintptr_t)rwlock, 0);
  }
  return result;
}

extern "C"
int __wrap_pthread_rwlock_destroy(pthread_rwlock_t *rwlock) {
  DECLARE_TID_AND_PC();
  Put(LOCK_DESTROY, tid, pc, (uintptr_t)rwlock, 0);  // before the actual call.
  return __real_pthread_rwlock_destroy(rwlock);
}

extern "C"
int __wrap_pthread_rwlock_wrlock(pthread_rwlock_t *rwlock) {
  DECLARE_TID_AND_PC();
  int result = __real_pthread_rwlock_wrlock(rwlock);
  if (result == 0) {
    Put(WRITER_LOCK, tid, pc, (uintptr_t)rwlock, 0);
  }
  return result;
}

extern "C"
int __wrap_pthread_rwlock_rdlock(pthread_rwlock_t *rwlock) {
  DECLARE_TID_AND_PC();
  int result = __real_pthread_rwlock_rdlock(rwlock);
  if (result == 0) {
    Put(READER_LOCK, tid, pc, (uintptr_t)rwlock, 0);
  }
  return result;
}

extern "C"
int __wrap_pthread_rwlock_unlock(pthread_rwlock_t *rwlock) {
  DECLARE_TID_AND_PC();
  Put(UNLOCK, tid, pc, (uintptr_t)rwlock, 0);
  return __real_pthread_rwlock_unlock(rwlock);
}

extern "C"
int __wrap_pthread_barrier_init(pthread_barrier_t *barrier,
                         const pthread_barrierattr_t *attr, unsigned count) {
  DECLARE_TID_AND_PC();
  Put(CYCLIC_BARRIER_INIT, tid, pc, (uintptr_t)barrier, count);
  return __real_pthread_barrier_init(barrier, attr, count);
}

extern "C"
int __wrap_pthread_barrier_wait(pthread_barrier_t *barrier) {
  DECLARE_TID_AND_PC();
  Put(CYCLIC_BARRIER_WAIT_BEFORE, tid, pc, (uintptr_t)barrier, 0);
  int result = __real_pthread_barrier_wait(barrier);
  Put(CYCLIC_BARRIER_WAIT_AFTER, tid, pc, (uintptr_t)barrier, 0);
  return result;
}


extern "C"
int __wrap_pthread_join(pthread_t thread, void **value_ptr) {
  // Note that the ThreadInfo of |thread| is valid no more.
  int tid = GetTid();
  int joined_tid = -1;
  {
    GIL scoped;
    if (Tids.find(thread) == Tids.end()) {
      InitConds[thread] = new pthread_cond_t;
      pthread_cond_init(InitConds[thread], NULL);
      DPrintf("T%d: Initializing InitConds[%p]=%p\n", tid, thread, InitConds[thread]);
      DPrintf("T%d (parent of %p): Waiting on InitConds[%p]=%p\n", tid, thread, thread, InitConds[thread]);
      __real_pthread_cond_wait(InitConds[thread], &global_lock);
    }
    assert (Tids.find(thread) != Tids.end());
    joined_tid = Tids[thread];
    DPrintf("T%d: Finished[T%d]=%d\n", tid, joined_tid, Finished[joined_tid]);
    if (Finished.find(joined_tid) == Finished.end()) {
      Finished[joined_tid] = false;
      DPrintf("T%d: setting Finished[T%d]=false\n", tid, joined_tid);
    }
    if (!Finished[joined_tid]) {
      FinishConds[joined_tid] = new pthread_cond_t;
      pthread_cond_init(FinishConds[joined_tid], NULL);
      DPrintf("T%d: Initializing FinishConds[%d]=%p\n", tid, joined_tid, FinishConds[joined_tid]);
      DPrintf("T%d (parent of T%d): Waiting on FinishConds[%d]=%p\n", tid, joined_tid, joined_tid, FinishConds[joined_tid]);
      __real_pthread_cond_wait(FinishConds[joined_tid], &global_lock);
    }
    unsafe_forget_thread(joined_tid, tid); // TODO(glider): earlier?
  }

  int result = __real_pthread_join(thread, value_ptr);
  {
    assert(joined_tid > 0);
    pc_t pc = GetPc();
    Put(THR_JOIN_AFTER, tid, pc, joined_tid, 0);
  }
  return result;
}

extern "C"
int __wrap_pthread_spin_init(pthread_spinlock_t *lock, int pshared) {
  int result = __real_pthread_spin_init(lock, pshared);
  if (result == 0) {
    int tid = GetTid();
    pc_t pc = GetPc();
    Put(UNLOCK_OR_INIT, tid, pc, (uintptr_t)lock, 0);
  }
  return result;
}

extern "C"
int __wrap_pthread_spin_destroy(pthread_spinlock_t *lock) {
  int tid = GetTid();
  pc_t pc = GetPc();
  Put(LOCK_DESTROY, tid, pc, (uintptr_t)lock, 0);
  return __real_pthread_spin_destroy(lock);
}


extern "C"
int __wrap_pthread_spin_lock(pthread_spinlock_t *lock) {
  int result = __real_pthread_spin_lock(lock);
  if (result == 0) {
    int tid = GetTid();
    pc_t pc = GetPc();
    Put(WRITER_LOCK, tid, pc, (uintptr_t)lock, 0);
  }
  return result;
}

extern "C"
int __wrap_pthread_spin_trylock(pthread_spinlock_t *lock) {
  int result = __real_pthread_spin_trylock(lock);
  if (result == 0) {
    int tid = GetTid();
    pc_t pc = GetPc();
    Put(WRITER_LOCK, tid, pc, (uintptr_t)lock, 0);
  }
  return result;
}

extern "C"
int __wrap_pthread_spin_unlock(pthread_spinlock_t *lock) {
  int tid = GetTid();
  pc_t pc = GetPc();
  Put(UNLOCK, tid, pc, (uintptr_t)lock, 0);
  return __real_pthread_spin_unlock(lock);
}


// }}}


// TODO(glider): update PCs

// STR* wrappers {{{1
extern
const char *strchr(const char *s, int c) {
  int tid = GetTid();
  pc_t pc = GetPc();
  return Replace_strchr(tid, pc, s, c);
}

extern
const char *strrchr(const char *s, int c) {
  int tid = GetTid();
  pc_t pc = GetPc();
  return Replace_strrchr(tid, pc, s, c);
}

extern "C"
size_t __wrap_strlen(const char *s) {
  int tid = GetTid();
  pc_t pc = GetPc();
  return Replace_strlen(tid, pc, s);
}

extern
void *memcpy(char *dest, const char *src, size_t n) {
  int tid = GetTid();
  pc_t pc = GetPc();
  return Replace_memcpy(tid, pc, dest, src, n);
}

extern
char *strcpy(char *dest, const char *src) {
  int tid = GetTid();
  pc_t pc = GetPc();
  return Replace_strcpy(tid, pc, dest, src);
}

extern "C"
void *__wrap_memchr(const char *s, int c, size_t n) {
  int tid = GetTid();
  pc_t pc = GetPc();
  return Replace_memchr(tid, pc, s, c, n);
}

// TODO(glider): replace all str*
//namespace std {
extern
void *memchr(void *s, int c, size_t n) {
  int tid = GetTid();
  pc_t pc = GetPc();
  return Replace_memchr(tid, pc, (char*)s, c, n);
}
//}

extern
int strcmp(const char *s1, const char *s2) {
  int tid = GetTid();
  pc_t pc = GetPc();
  return Replace_strcmp(tid, pc, s1, s2);
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
  Put(WAIT, tid, pc, (uintptr_t)worker, 0);
  (*worker)();
}

extern "C"
int __wrap_atexit(void (*function)(void)) {
  DECLARE_TID_AND_PC();
  push_atexit(function);
  int result = __real_atexit(atexit_callback);
  Put(SIGNAL, tid, pc, kAtExitMagic, 0);  // TODO(glider): do we need it?
  Put(SIGNAL, tid, pc, (uintptr_t)function, 0);
  return result;
}

extern "C"
void __wrap_exit(int status) {
  DECLARE_TID_AND_PC();
  Put(WAIT, tid, pc, kAtExitMagic, 0);
  __real_exit(status);
}


// }}}

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
  // TODO(glider): we should treat dup()ped fds as equal ones.
  DECLARE_TID_AND_PC();
  ssize_t result = __real_read(fd, buf, count);
  // It only makes sense to wait for a previous write, not EOF.
  if (result > 0) {
    Put(WAIT, tid, pc, FdMagic(fd), 0);
  }
  return result;
}

extern "C"
ssize_t __wrap_write(int fd, const void *buf, size_t count) {
  DECLARE_TID_AND_PC();
  Put(SIGNAL, tid, pc, FdMagic(fd), 0);
  return __real_write(fd, buf, count);
}

// instrumentation API {{{1
extern "C"
void rtn_call(void *addr) {
  int tid = GetTid();
  GIL scoped;
  UPut(RTN_CALL, tid, 0, (uintptr_t)addr, 0);
  INFO.passport_index++;
  Passport &passport = INFO.passport[INFO.passport_index];
  passport.mop_info = NULL;
  passport.num_mops = 0;
  passport.flushed = 0;
  passport.known_bb_addr = addr;
}

extern "C"
void rtn_exit() {
  int tid = GetTid();
  GIL scoped;
  unsafe_flush_tleb();
  INFO.passport_index--;
  UPut(RTN_EXIT, tid, 0, 0, 0);
}
// }}}

// dynamic_annotations {{{1
extern "C"
void AnnotateCondVarSignal(const char *file, int line, void *cv) {
  int tid = GetTid();
  pc_t pc = __LINE__;
  GIL scoped;
  //fprintf(out_file, "SIGNAL %d 0 %p 0\n", tid, cv);
  UPut(SIGNAL, tid, pc, (uintptr_t)cv, 0);
}

extern"C"
void AnnotateCondVarWait(const char *file, int line, void *cv, void *lock) {
  DECLARE_TID_AND_PC();
  Put(WAIT, tid, pc, (uintptr_t)cv, 0);
}

extern "C"
void AnnotateTraceMemory(char *file, int line, void *mem) {
  DECLARE_TID_AND_PC();
  Put(TRACE_MEM, tid, pc, (uintptr_t)mem, 0);
}

extern "C"
void AnnotateFlushState(char *file, int line) {
  DECLARE_TID_AND_PC();
  Put(TRACE_MEM, tid, pc, 0, 0);
}

extern "C"
void AnnotateEnableRaceDetection(char *file, int line, int enable) {
  GIL scoped;
  global_ignore = !enable;
  fprintf(stderr, "enable: %d, global_ignore: %d\n", enable, global_ignore);
}


extern "C"
void AnnotateMutexIsUsedAsCondVar(char *file, int line, void *mu) {
  DECLARE_TID_AND_PC();
  Put(HB_LOCK, tid, pc, (uintptr_t)mu, 0);
}

extern "C"
void AnnotatePCQGet(const char *file, int line, void *pcq) {
  DECLARE_TID_AND_PC();
  Put(PCQ_GET, tid, pc, (uintptr_t)pcq, 0);
}

extern "C"
void AnnotatePCQPut(const char *file, int line, void *pcq) {
  DECLARE_TID_AND_PC();
  Put(PCQ_PUT, tid, pc, (uintptr_t)pcq, 0);
}

extern "C"
void AnnotatePCQDestroy(const char *file, int line, void *pcq) {
  DECLARE_TID_AND_PC();
  Put(PCQ_DESTROY, tid, pc, (uintptr_t)pcq, 0);
}

extern "C"
void AnnotatePCQCreate(const char *file, int line, void *pcq) {
  DECLARE_TID_AND_PC();
  Put(PCQ_CREATE, tid, pc, (uintptr_t)pcq, 0);
}


extern "C"
void AnnotateExpectRace(const char *file, int line,
                        void *mem, char *description) {
  int tid = GetTid();
  Put(EXPECT_RACE, tid, (uintptr_t)description, (uintptr_t)mem, 1);
}

extern "C"
void AnnotateBenignRace(const char *file, int line,
                        void *mem, char *description) {
  DECLARE_TID();
  Put(BENIGN_RACE, tid, (uintptr_t)description, (uintptr_t)mem, 1);
}

extern "C"
void AnnotateBenignRaceSized(const char *file, int line,
                        void *mem, long size, char *description) {
  DECLARE_TID();
  Put(BENIGN_RACE, tid, (uintptr_t)description,
      (uintptr_t)mem, (uintptr_t)size);
}

// TODO(glider): that should be enough to keep the ThreadSanitizerQuery from
// thread_sanitizer.cc
/*
extern "C"
const char *ThreadSanitizerQuery(const char *query) {
  Printf("QUERY: %s\n", query);
  return const_cast<char*>(ThreadSanitizerQuery(query));
}
*/
extern "C"
void AnnotateIgnoreReadsBegin(char *file, int line, void *mu) {
  int tid = GetTid();
  pc_t pc = GetPc();
  Put(IGNORE_READS_BEG, tid, pc, 0, 0);
  DPrintf("IGNORE_READS_BEG @%p\n", mu);
}

extern "C"
void AnnotateIgnoreReadsEnd(char *file, int line, void *mu) {
  int tid = GetTid();
  pc_t pc = GetPc();
  Put(IGNORE_READS_END, tid, pc, 0, 0);
  DPrintf("IGNORE_READS_END @%p\n", mu);
}

extern "C"
void AnnotateIgnoreWritesBegin(char *file, int line, void *mu) {
  int tid = GetTid();
  pc_t pc = GetPc();
  Put(IGNORE_WRITES_BEG, tid, pc, 0, 0);
  DPrintf("IGNORE_WRITES_BEG @%p\n", mu);
}

extern "C"
void AnnotateIgnoreWritesEnd(char *file, int line, void *mu) {
  int tid = GetTid();
  pc_t pc = GetPc();
  Put(IGNORE_WRITES_END, tid, pc, 0, 0);
  DPrintf("IGNORE_WRITES_END @%p\n", mu);
}
// }}}

// Debug info routines {{{1
void ReadDbgInfo(string filename) {
  GIL scoped; // TODO(glider): this should be some different lock.
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
    info.fun = parts[1];
    info.file = parts[2];
    info.path = parts[4];
    (*debug_info)[info.pc] = info;
  }
}



string PcToRtnName(pc_t pc, bool demangle) {
  GIL scoped; // TODO(glider): this should be some different lock.
  if (debug_info && (debug_info->find(pc) != debug_info->end())) {
    return (*debug_info)[pc].fun;
  }
  return "";
}

void PcToStrings(pc_t pc, bool demangle,
                 string *img_name, string *rtn_name,
                 string *file_name, int *line_no) {
  if (debug_info && (debug_info->find(pc) != debug_info->end())) {
    img_name = NULL;
    *rtn_name = (*debug_info)[pc].fun;
    *file_name = ((*debug_info)[pc].file);
    *line_no = ((*debug_info)[pc].line);
  } else {
    img_name = NULL;
    rtn_name = NULL;
    file_name = NULL;
    line_no = NULL;
  }
}
// }}}
