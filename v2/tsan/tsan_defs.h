//===-- tsan_defs.h ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
//===----------------------------------------------------------------------===//

#ifndef TSAN_DEFS_H
#define TSAN_DEFS_H

#include "tsan_compiler.h"

#ifndef TSAN_DEBUG
#define TSAN_DEBUG 0
#endif  // TSAN_DEBUG

namespace __tsan {

typedef unsigned u32;  // NOLINT
typedef unsigned long long u64;  // NOLINT
typedef   signed long long s64;  // NOLINT
typedef unsigned long uptr;  // NOLINT

const uptr kPageSize = 4096;
const int kTidBits = 16;
const int kMaxTid = 1 << kTidBits;
const int kClkBits = 40;

#ifdef TSAN_SHADOW_COUNT
# if TSAN_SHADOW_COUNT == 2 \
  || TSAN_SHADOW_COUNT == 4 || TSAN_SHADOW_COUNT == 8
const unsigned kShadowCnt = TSAN_SHADOW_COUNT;
# else
#   error "TSAN_SHADOW_COUNT must be one of 2,4,8"
# endif
#else
// Count of shadow values in a shadow cell.
const unsigned kShadowCnt = 8;
#endif

// That many user bytes are mapped onto a single shadow cell.
const unsigned kShadowCell = 8;

// Size of a single shadow value (u64).
const unsigned kShadowSize = 8;

#if defined(TSAN_COLLECT_STATS) && TSAN_COLLECT_STATS
const bool kCollectStats = true;
#else
const bool kCollectStats = false;
#endif

#define CHECK_IMPL(c1, op, c2) \
  do { \
    __tsan::u64 v1 = (u64)(c1); \
    __tsan::u64 v2 = (u64)(c2); \
    if (!(v1 op v2)) \
      __tsan::CheckFailed(__FILE__, __LINE__, \
        "(" #c1 ") " #op " (" #c2 ")", v1, v2); \
  } while (false) \
/**/

#define CHECK(a)       CHECK_IMPL((a), !=, 0)
#define CHECK_EQ(a, b) CHECK_IMPL((a), ==, (b))
#define CHECK_NE(a, b) CHECK_IMPL((a), !=, (b))
#define CHECK_LT(a, b) CHECK_IMPL((a), <,  (b))
#define CHECK_LE(a, b) CHECK_IMPL((a), <=, (b))
#define CHECK_GT(a, b) CHECK_IMPL((a), >,  (b))
#define CHECK_GE(a, b) CHECK_IMPL((a), >=, (b))

#if TSAN_DEBUG
#define DCHECK(a)       CHECK(a)
#define DCHECK_EQ(a, b) CHECK_EQ(a, b)
#define DCHECK_NE(a, b) CHECK_NE(a, b)
#define DCHECK_LT(a, b) CHECK_LT(a, b)
#define DCHECK_LE(a, b) CHECK_LE(a, b)
#define DCHECK_GT(a, b) CHECK_GT(a, b)
#define DCHECK_GE(a, b) CHECK_GE(a, b)
#else
#define DCHECK(a)
#define DCHECK_EQ(a, b)
#define DCHECK_NE(a, b)
#define DCHECK_LT(a, b)
#define DCHECK_LE(a, b)
#define DCHECK_GT(a, b)
#define DCHECK_GE(a, b)
#endif

void CheckFailed(const char *file, int line, const char *cond, u64 v1, u64 v2);

template<typename T>
T min(T a, T b) {
  return a < b ? a : b;
}

template<typename T>
T max(T a, T b) {
  return a > b ? a : b;
}

template<typename T>
T RoundUp(T p, int align) {
  DCHECK_EQ(align & (align - 1), 0);
  return (T)(((u64)p + align - 1) & ~(align - 1));
}

void internal_memset(void *ptr, int c, uptr size);
void internal_memcpy(void *dst, const void *src, uptr size);
int internal_strcmp(const char *s1, const char *s2);
int internal_strncmp(const char *s1, const char *s2, uptr size);
void internal_strcpy(char *s1, const char *s2);
uptr internal_strlen(const char *s);
const char *internal_strstr(const char *where, const char *what);
const char *internal_strchr(const char *where, char what);

struct MD5Hash {
  u64 hash[2];
  bool operator==(const MD5Hash &other) const {
    return hash[0] == other.hash[0] && hash[1] == other.hash[1];
  }
};

MD5Hash md5_hash(const void *data, uptr size);

enum StatType {
  // Memory access processing related stuff.
  StatMop,
  StatMopRead,
  StatMopWrite,
  StatMop1,  // These must be consequtive.
  StatMop2,
  StatMop4,
  StatMop8,
  StatMopSame,
  StatMopRange,
  StatShadowProcessed,
  StatShadowZero,
  StatShadowNonZero,  // Derived.
  StatShadowSameSize,
  StatShadowIntersect,
  StatShadowNotIntersect,
  StatShadowSameThread,
  StatShadowAnotherThread,
  StatShadowReplace,

  // Func processing.
  StatFuncEnter,
  StatFuncExit,

  // Trace processing.
  StatEvents,

  // Threads.
  StatThreadCreate,
  StatThreadFinish,
  StatThreadReuse,
  StatThreadMaxTid,
  StatThreadMaxAlive,

  // Mutexes.
  StatMutexCreate,
  StatMutexDestroy,
  StatMutexLock,
  StatMutexUnlock,
  StatMutexRecLock,
  StatMutexRecUnlock,
  StatMutexReadLock,
  StatMutexReadUnlock,

  // Synchronization.
  StatSyncCreated,
  StatSyncDestroyed,
  StatSyncAcquire,
  StatSyncRelease,

  // Atomics.
  StatAtomic,
  StatAtomicLoad,
  StatAtomicStore,
  StatAtomicExchange,
  StatAtomicFetchAdd,
  StatAtomicCAS,
  StatAtomicFence,
  StatAtomicRelaxed,
  StatAtomicConsume,
  StatAtomicAcquire,
  StatAtomicRelease,
  StatAtomicAcq_Rel,
  StatAtomicSeq_Cst,
  StatAtomic1,
  StatAtomic2,
  StatAtomic4,
  StatAtomic8,

  // Interceptors.
  StatInterceptor,
  StatInt_malloc,
  StatInt_calloc,
  StatInt_realloc,
  StatInt_free,
  StatInt_cfree,
  StatInt_mmap,
  StatInt_mmap64,
  StatInt_munmap,
  StatInt_memalign,
  StatInt_valloc,
  StatInt_pvalloc,
  StatInt_posix_memalign,
  StatInt__Znwm,
  StatInt__ZnwmRKSt9nothrow_t,
  StatInt__Znam,
  StatInt__ZnamRKSt9nothrow_t,
  StatInt__ZdlPv,
  StatInt__ZdlPvRKSt9nothrow_t,
  StatInt__ZdaPv,
  StatInt__ZdaPvRKSt9nothrow_t,
  StatInt_strlen,
  StatInt_memset,
  StatInt_memcpy,
  StatInt_strcmp,
  StatInt_memchr,
  StatInt_memrchr,
  StatInt_memmove,
  StatInt_memcmp,
  StatInt_strchr,
  StatInt_strchrnul,
  StatInt_strrchr,
  StatInt_strncmp,
  StatInt_strcpy,
  StatInt_strncpy,
  StatInt_strstr,
  StatInt_atexit,
  StatInt___cxa_guard_acquire,
  StatInt___cxa_guard_release,
  StatInt_pthread_create,
  StatInt_pthread_join,
  StatInt_pthread_detach,
  StatInt_pthread_mutex_init,
  StatInt_pthread_mutex_destroy,
  StatInt_pthread_mutex_lock,
  StatInt_pthread_mutex_trylock,
  StatInt_pthread_mutex_timedlock,
  StatInt_pthread_mutex_unlock,
  StatInt_pthread_spin_init,
  StatInt_pthread_spin_destroy,
  StatInt_pthread_spin_lock,
  StatInt_pthread_spin_trylock,
  StatInt_pthread_spin_unlock,
  StatInt_pthread_rwlock_init,
  StatInt_pthread_rwlock_destroy,
  StatInt_pthread_rwlock_rdlock,
  StatInt_pthread_rwlock_tryrdlock,
  StatInt_pthread_rwlock_timedrdlock,
  StatInt_pthread_rwlock_wrlock,
  StatInt_pthread_rwlock_trywrlock,
  StatInt_pthread_rwlock_timedwrlock,
  StatInt_pthread_rwlock_unlock,
  StatInt_pthread_cond_init,
  StatInt_pthread_cond_destroy,
  StatInt_pthread_cond_signal,
  StatInt_pthread_cond_broadcast,
  StatInt_pthread_cond_wait,
  StatInt_pthread_cond_timedwait,
  StatInt_pthread_barrier_init,
  StatInt_pthread_barrier_destroy,
  StatInt_pthread_barrier_wait,
  StatInt_pthread_once,
  StatInt_sem_init,
  StatInt_sem_destroy,
  StatInt_sem_wait,
  StatInt_sem_trywait,
  StatInt_sem_timedwait,
  StatInt_sem_post,
  StatInt_sem_getvalue,
  StatInt_read,
  StatInt_pread,
  StatInt_pread64,
  StatInt_readv,
  StatInt_preadv64,
  StatInt_write,
  StatInt_pwrite,
  StatInt_pwrite64,
  StatInt_writev,
  StatInt_pwritev64,
  StatInt_send,
  StatInt_sendmsg,
  StatInt_recv,
  StatInt_recvmsg,
  StatInt_unlink,
  StatInt_fopen,
  StatInt_fread,
  StatInt_fwrite,
  StatInt_puts,
  StatInt_rmdir,
  StatInt_opendir,
  StatInt_epoll_ctl,
  StatInt_epoll_wait,
  StatInt_sigaction,

  // Dynamic annotations.
  StatAnnotation,
  StatAnnotateHappensBefore,
  StatAnnotateHappensAfter,
  StatAnnotateCondVarSignal,
  StatAnnotateCondVarSignalAll,
  StatAnnotateMutexIsNotPHB,
  StatAnnotateCondVarWait,
  StatAnnotateRWLockCreate,
  StatAnnotateRWLockDestroy,
  StatAnnotateRWLockAcquired,
  StatAnnotateRWLockReleased,
  StatAnnotateTraceMemory,
  StatAnnotateFlushState,
  StatAnnotateNewMemory,
  StatAnnotateNoOp,
  StatAnnotateFlushExpectedRaces,
  StatAnnotateEnableRaceDetection,
  StatAnnotateMutexIsUsedAsCondVar,
  StatAnnotatePCQGet,
  StatAnnotatePCQPut,
  StatAnnotatePCQDestroy,
  StatAnnotatePCQCreate,
  StatAnnotateExpectRace,
  StatAnnotateBenignRaceSized,
  StatAnnotateBenignRace,
  StatAnnotateIgnoreReadsBegin,
  StatAnnotateIgnoreReadsEnd,
  StatAnnotateIgnoreWritesBegin,
  StatAnnotateIgnoreWritesEnd,
  StatAnnotatePublishMemoryRange,
  StatAnnotateUnpublishMemoryRange,
  StatAnnotateThreadName,

  // Internal mutex contentionz.
  StatMtxTotal,
  StatMtxTrace,
  StatMtxThreads,
  StatMtxReport,
  StatMtxSyncVar,
  StatMtxSyncTab,
  StatMtxSlab,
  StatMtxAnnotations,
  StatMtxAtExit,

  // This must be last.
  StatCnt,
};

struct ThreadState;
struct ThreadContext;
struct Context;
struct ReportDesc;
struct ReportStack;
class RegionAlloc;
class StackTrace;

}  // namespace __tsan

#endif  // TSAN_DEFS_H
