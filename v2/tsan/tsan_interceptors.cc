//===-- tsan_interceptors_linux.cc ------------------------------*- C++ -*-===//
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

#include "interception/interception.h"
#include "tsan_rtl.h"
#include "tsan_interface.h"
#include "tsan_atomic.h"
#include "tsan_platform.h"

using namespace __tsan;  // NOLINT

extern "C" int pthread_attr_getdetachstate(void *attr, int *v);
extern "C" int pthread_key_create(unsigned *key, void (*destructor)(void* v));
extern "C" int pthread_setspecific(unsigned key, const void *v);
extern "C" int pthread_mutexattr_gettype(void *a, int *type);
extern "C" int pthread_yield();
extern "C" void *pthread_self();
extern "C" void *__libc_malloc(uptr size);
extern "C" void *__libc_calloc(uptr nmemb, uptr size);
extern "C" void __libc_free(void *ptr);
extern "C" void *__libc_realloc(void *ptr, uptr size);
extern "C" int atexit(void (*function)());
extern "C" void _exit(int status);
extern "C" int *__errno_location();
const int PTHREAD_MUTEX_RECURSIVE = 1;
const int PTHREAD_MUTEX_RECURSIVE_NP = 1;
const int EINVAL = 22;
const int EPOLL_CTL_ADD = 1;
void *const MAP_FAILED = (void*)-1;
const int PTHREAD_BARRIER_SERIAL_THREAD = -1;
const int MAP_FIXED = 0x10;
typedef long long_t;  // NOLINT

static unsigned g_thread_finalize_key;

template<typename T>
T min(T a, T b) {
  return a < b ? a : b;
}

class ScopedInterceptor {
 public:
  ScopedInterceptor(ThreadState *thr, const char *fname, uptr pc)
      : thr_(thr)
      , in_rtl_(thr->in_rtl) {
    if (thr_->in_rtl == 0) {
      Initialize(thr);
      DPrintf("#%d: %s\n", thr_->tid, fname);
      FuncEntry(thr, pc);
    }
    thr_->in_rtl++;
  }

  ~ScopedInterceptor() {
    thr_->in_rtl--;
    if (thr_->in_rtl == 0) {
      FuncExit(thr_);
    }
    CHECK_EQ(in_rtl_, thr_->in_rtl);
  }
 private:
  ThreadState *const thr_;
  const int in_rtl_;
};

#define SCOPED_INTERCEPTOR_RAW(func, ...) \
    ThreadState *thr = cur_thread(); \
    ScopedInterceptor si(thr, #func, \
        (__tsan::uptr)__builtin_return_address(0)); \
    const uptr pc = (uptr)func; \
    (void)pc; \
/**/

#define SCOPED_INTERCEPTOR(func, ...) \
    SCOPED_INTERCEPTOR_RAW(func, __VA_ARGS__); \
    if (thr->in_rtl > 1) \
      return REAL(func)(__VA_ARGS__); \
/**/

static void finalize() {
  int status = Finalize(cur_thread());
  if (status)
    _exit(status);
}

static uptr fd2addr(int fd) {
  (void)fd;
  static u64 addr;
  return (uptr)&addr;
}

static uptr epollfd2addr(int fd) {
  (void)fd;
  static u64 addr;
  return (uptr)&addr;
}

static uptr file2addr(char *path) {
  (void)path;
  static u64 addr;
  return (uptr)&addr;
}

static uptr dir2addr(char *path) {
  (void)path;
  static u64 addr;
  return (uptr)&addr;
}

INTERCEPTOR(void*, malloc, uptr size) {
  SCOPED_INTERCEPTOR_RAW(malloc, size);
  if (thr->in_rtl > 1)
    return __libc_malloc(size);
  void *p = __libc_malloc(size);
  if (p != 0) {
    MemoryResetRange(thr, pc, (uptr)p, size);
  }
  return p;
}

INTERCEPTOR(void*, calloc, uptr size, uptr n) {
  SCOPED_INTERCEPTOR_RAW(calloc, size, n);
  if (thr->in_rtl > 1)
    return __libc_calloc(size, n);
  void *p = __libc_calloc(size, n);
  if (p != 0) {
    MemoryResetRange(thr, pc, (uptr)p, n * size);
  }
  return p;
}

INTERCEPTOR(void*, realloc, void *p, uptr size) {
  SCOPED_INTERCEPTOR_RAW(realloc, p, size);
  if (thr->in_rtl > 1)
    return __libc_realloc(p, size);
  void *p2 = __libc_realloc(p, size);
  if (p2 != 0 && size != 0) {
    MemoryResetRange(thr, pc, (uptr)p2, size);
  }
  return p2;
}

INTERCEPTOR(void, free, void *p) {
  if (p == 0)
    return;
  SCOPED_INTERCEPTOR_RAW(free, p);
  if (thr->in_rtl > 1)
    return __libc_free(p);
  uptr size = 8;  // FIXME: Use real size.
  MemoryRangeFreed(thr, pc, (uptr)p, size);
  __libc_free(p);
}

INTERCEPTOR(void, cfree, void *p) {
  if (p == 0)
    return;
  SCOPED_INTERCEPTOR_RAW(cfree, p);
  if (thr->in_rtl > 1)
    return __libc_free(p);
  uptr size = 8;  // FIXME: Use real size.
  MemoryRangeFreed(thr, pc, (uptr)p, size);
  __libc_free(p);
}

INTERCEPTOR(uptr, strlen, void *s) {
  SCOPED_INTERCEPTOR(strlen, s);
  uptr len = REAL(strlen)(s);
  MemoryAccessRange(thr, pc, (uptr)s, len + 1, false);
  return len;
}

INTERCEPTOR(void*, memset, void *dst, int v, uptr size) {
  SCOPED_INTERCEPTOR(memset, dst, v, size);
  MemoryAccessRange(thr, pc, (uptr)dst, size, true);
  return REAL(memset)(dst, v, size);
}

INTERCEPTOR(void*, memcpy, void *dst, const void *src, uptr size) {
  SCOPED_INTERCEPTOR(memcpy, dst, src, size);
  MemoryAccessRange(thr, pc, (uptr)dst, size, true);
  MemoryAccessRange(thr, pc, (uptr)src, size, false);
  return REAL(memcpy)(dst, src, size);
}

INTERCEPTOR(int, strcmp, signed char *s1, signed char *s2) {
  SCOPED_INTERCEPTOR(strcmp, s1, s2);
  uptr len = 0;
  for (; s1[len] && s2[len]; len++) {
    if (s1[len] != s2[len])
      break;
  }
  MemoryAccessRange(thr, pc, (uptr)s1, len + 1, false);
  MemoryAccessRange(thr, pc, (uptr)s2, len + 1, false);
  return s1[len] - s2[len];
}

INTERCEPTOR(int, strncmp, signed char *s1, signed char *s2, uptr n) {
  SCOPED_INTERCEPTOR(strncmp, s1, s2, n);
  uptr len = 0;
  for (; s1[len] && s2[len] && len < n; len++) {
    if (s1[len] != s2[len])
      break;
  }
  MemoryAccessRange(thr, pc, (uptr)s1, len + 1, false);
  MemoryAccessRange(thr, pc, (uptr)s2, len + 1, false);
  return s1[len] - s2[len];
}

INTERCEPTOR(void*, memchr, void *s, int c, uptr n) {
  SCOPED_INTERCEPTOR(memchr, s, c, n);
  void *res = REAL(memchr)(s, c, n);
  uptr len = res ? (char*)res - (char*)s + 1 : n;
  MemoryAccessRange(thr, pc, (uptr)s, len, false);
  return res;
}

INTERCEPTOR(void*, memrchr, char *s, int c, uptr n) {
  SCOPED_INTERCEPTOR(memrchr, s, c, n);
  MemoryAccessRange(thr, pc, (uptr)s, n, false);
  return REAL(memrchr)(s, c, n);
}

INTERCEPTOR(void*, memmove, void *dst, void *src, uptr n) {
  SCOPED_INTERCEPTOR(memmove, dst, src, n);
  MemoryAccessRange(thr, pc, (uptr)dst, n, true);
  MemoryAccessRange(thr, pc, (uptr)src, n, false);
  return REAL(memmove)(dst, src, n);
}

INTERCEPTOR(int, memcmp, void *s1, void *s2, uptr n) {
  SCOPED_INTERCEPTOR(memcmp, s1, s2, n);
  int res = 0;
  uptr len = 0;
  for (; len < n; len++) {
    if ((res = ((signed char*)s1)[len] - ((signed char*)s2)[len]))
      break;
  }
  MemoryAccessRange(thr, pc, (uptr)s1, len + 1, false);
  MemoryAccessRange(thr, pc, (uptr)s2, len + 1, false);
  return res;
}

INTERCEPTOR(void*, strchr, void *s, int c) {
  SCOPED_INTERCEPTOR(strchr, s, c);
  void *res = REAL(strchr)(s, c);
  uptr len = res ? (char*)res - (char*)s + 1 : REAL(strlen)(s) + 1;
  MemoryAccessRange(thr, pc, (uptr)s, len, false);
  return res;
}

INTERCEPTOR(void*, strchrnul, void *s, int c) {
  SCOPED_INTERCEPTOR(strchrnul, s, c);
  void *res = REAL(strchrnul)(s, c);
  uptr len = (char*)res - (char*)s + 1;
  MemoryAccessRange(thr, pc, (uptr)s, len, false);
  return res;
}

INTERCEPTOR(void*, strrchr, void *s, int c) {
  SCOPED_INTERCEPTOR(strrchr, s, c);
  MemoryAccessRange(thr, pc, (uptr)s, REAL(strlen)(s) + 1, false);
  return REAL(strrchr)(s, c);
}

INTERCEPTOR(void*, strcpy, void *dst, void *src) {  // NOLINT
  SCOPED_INTERCEPTOR(strcpy, dst, src);  // NOLINT
  uptr srclen = REAL(strlen)(src);
  MemoryAccessRange(thr, pc, (uptr)dst, srclen + 1, true);
  MemoryAccessRange(thr, pc, (uptr)src, srclen + 1, false);
  return REAL(strcpy)(dst, src);  // NOLINT
}

INTERCEPTOR(void*, strncpy, void *dst, void *src, uptr n) {
  SCOPED_INTERCEPTOR(strncpy, dst, src, n);
  uptr srclen = REAL(strlen)(src);
  MemoryAccessRange(thr, pc, (uptr)dst, n, true);
  MemoryAccessRange(thr, pc, (uptr)src, min(srclen + 1, n), false);
  return REAL(strncpy)(dst, src, n);
}

static bool fix_mmap_addr(void **addr, long_t sz, int flags) {
  if (*addr) {
    if (!IsAppMem((uptr)*addr) || !IsAppMem((uptr)*addr + sz - 1)) {
      if (flags & MAP_FIXED) {
        *__errno_location() = EINVAL;
        return false;
      } else {
        *addr = 0;
      }
    }
  }
  return true;
}

INTERCEPTOR(void*, mmap, void *addr, long_t sz, int prot,
                         int flags, int fd, unsigned off) {
  SCOPED_INTERCEPTOR(mmap, addr, sz, prot, flags, fd, off);
  if (!fix_mmap_addr(&addr, sz, flags))
    return MAP_FAILED;
  void *res = REAL(mmap)(addr, sz, prot, flags, fd, off);
  if (res != MAP_FAILED) {
    MemoryResetRange(thr, pc, (uptr)res, sz);
  }
  return res;
}

INTERCEPTOR(void*, mmap64, void *addr, long_t sz, int prot,
                           int flags, int fd, u64 off) {
  SCOPED_INTERCEPTOR(mmap64, addr, sz, prot, flags, fd, off);
  if (!fix_mmap_addr(&addr, sz, flags))
    return MAP_FAILED;
  void *res = REAL(mmap64)(addr, sz, prot, flags, fd, off);
  if (res != MAP_FAILED) {
    MemoryResetRange(thr, pc, (uptr)res, sz);
  }
  return res;
}

INTERCEPTOR(int, munmap, void *addr, long_t sz) {
  SCOPED_INTERCEPTOR(munmap, addr, sz);
  MemoryRangeFreed(thr, pc, (uptr)addr, sz);
  int res = REAL(munmap)(addr, sz);
  return res;
}

#ifdef __LP64__

INTERCEPTOR(void*, _Znwm, uptr sz) {
  SCOPED_INTERCEPTOR(_Znwm, sz);
  void *res = REAL(_Znwm)(sz);
  if (res != 0) {
    MemoryResetRange(thr, pc, (uptr)res, sz);
  }
  return res;
}

INTERCEPTOR(void*, _ZnwmRKSt9nothrow_t, uptr sz) {
  SCOPED_INTERCEPTOR(_ZnwmRKSt9nothrow_t, sz);
  void *res = REAL(_ZnwmRKSt9nothrow_t)(sz);
  if (res != 0) {
    MemoryResetRange(thr, pc, (uptr)res, sz);
  }
  return res;
}

INTERCEPTOR(void*, _Znam, uptr sz) {
  SCOPED_INTERCEPTOR(_Znam, sz);
  void *res = REAL(_Znam)(sz);
  if (res != 0) {
    MemoryResetRange(thr, pc, (uptr)res, sz);
  }
  return res;
}

INTERCEPTOR(void*, _ZnamRKSt9nothrow_t, uptr sz) {
  SCOPED_INTERCEPTOR(_ZnamRKSt9nothrow_t, sz);
  void *res = REAL(_ZnamRKSt9nothrow_t)(sz);
  if (res != 0) {
    MemoryResetRange(thr, pc, (uptr)res, sz);
  }
  return res;
}

#else
#error "Not implemented"
#endif

INTERCEPTOR(void, _ZdlPv, void *p) {
  if (p == 0)
    return;
  SCOPED_INTERCEPTOR(_ZdlPv, p);
  uptr size = 8;  // FIXME: Use real size.
  MemoryRangeFreed(thr, pc, (uptr)p, size);
  REAL(_ZdlPv)(p);
}

INTERCEPTOR(void, _ZdlPvRKSt9nothrow_t, void *p) {
  if (p == 0)
    return;
  SCOPED_INTERCEPTOR(_ZdlPvRKSt9nothrow_t, p);
  uptr size = 8;  // FIXME: Use real size.
  MemoryRangeFreed(thr, pc, (uptr)p, size);
  REAL(_ZdlPvRKSt9nothrow_t)(p);
}

INTERCEPTOR(void, _ZdaPv, void *p) {
  if (p == 0)
    return;
  SCOPED_INTERCEPTOR(_ZdaPv, p);
  uptr size = 8;  // FIXME: Use real size.
  MemoryRangeFreed(thr, pc, (uptr)p, size);
  REAL(_ZdaPv)(p);
}

INTERCEPTOR(void, _ZdaPvRKSt9nothrow_t, void *p) {
  if (p == 0)
    return;
  SCOPED_INTERCEPTOR(_ZdaPvRKSt9nothrow_t, p);
  uptr size = 8;  // FIXME: Use real size.
  MemoryRangeFreed(thr, pc, (uptr)p, size);
  REAL(_ZdaPvRKSt9nothrow_t)(p);
}

// int posix_memalign(void **memptr, size_t alignment, size_t size);
// void *valloc(size_t size);
// Equivalent to valloc(minimum-page-that-holds(n)), that is, round up
// __size to nearest pagesize.
// extern void * pvalloc(size_t __size)
// void *memalign(size_t boundary, size_t size);

static void thread_finalize(void *v) {
  uptr iter = (uptr)v;
  if (iter > 1) {
    if (pthread_setspecific(g_thread_finalize_key, (void*)(iter - 1))) {
      Printf("ThreadSanitizer: failed to set thread key\n");
      Die();
    }
    return;
  }
  ThreadFinish(cur_thread());
}


struct ThreadParam {
  void* (*callback)(void *arg);
  void *param;
  atomic_uintptr_t tid;
};

extern "C" void *__tsan_thread_start_func(void *arg) {
  ThreadParam *p = (ThreadParam*)arg;
  void* (*callback)(void *arg) = p->callback;
  void *param = p->param;
  int tid = 0;
  {
    ThreadState *thr = cur_thread();
    thr->in_rtl++;
    if (pthread_setspecific(g_thread_finalize_key, (void*)4)) {
      Printf("ThreadSanitizer: failed to set thread key\n");
      Die();
    }
    while ((tid = atomic_load(&p->tid, memory_order_acquire)) == 0)
      pthread_yield();
    atomic_store(&p->tid, 0, memory_order_release);
    ThreadStart(thr, tid);
    CHECK_EQ(thr->in_rtl, 0);  // ThreadStart() resets it to zero.
  }
  void *res = callback(param);
  // Prevent the callback from being tail called,
  // it mixes up stack traces.
  volatile int foo = 42;
  foo++;
  return res;
}

INTERCEPTOR(int, pthread_create,
    void *th, void *attr, void *(*callback)(void*), void * param) {
  SCOPED_INTERCEPTOR(pthread_create, th, attr, callback, param);
  int detached = 0;
  if (attr)
    pthread_attr_getdetachstate(attr, &detached);
  ThreadParam p;
  p.callback = callback;
  p.param = param;
  atomic_store(&p.tid, 0, memory_order_relaxed);
  int res = REAL(pthread_create)(th, attr, __tsan_thread_start_func, &p);
  if (res == 0) {
    int tid = ThreadCreate(cur_thread(), pc, *(uptr*)th, detached);
    CHECK_NE(tid, 0);
    atomic_store(&p.tid, tid, memory_order_release);
    while (atomic_load(&p.tid, memory_order_acquire) != 0)
      pthread_yield();
  }
  return res;
}

INTERCEPTOR(int, pthread_join, void *th, void **ret) {
  SCOPED_INTERCEPTOR(pthread_join, th, ret);
  int res = REAL(pthread_join)(th, ret);
  if (res == 0) {
    ThreadJoin(cur_thread(), pc, (uptr)th);
  }
  return res;
}

INTERCEPTOR(int, pthread_detach, void *th) {
  SCOPED_INTERCEPTOR(pthread_detach, th);
  int res = REAL(pthread_detach)(th);
  if (res == 0) {
    ThreadDetach(cur_thread(), pc, (uptr)th);
  }
  return res;
}

INTERCEPTOR(int, pthread_mutex_init, void *m, void *a) {
  SCOPED_INTERCEPTOR(pthread_mutex_init, m, a);
  int res = REAL(pthread_mutex_init)(m, a);
  if (res == 0) {
    bool recursive = false;
    if (a) {
      int type = 0;
      if (pthread_mutexattr_gettype(a, &type) == 0)
        recursive = (type == PTHREAD_MUTEX_RECURSIVE
            || type == PTHREAD_MUTEX_RECURSIVE_NP);
    }
    MutexCreate(cur_thread(), pc, (uptr)m, false, recursive);
  }
  return res;
}

INTERCEPTOR(int, pthread_mutex_destroy, void *m) {
  SCOPED_INTERCEPTOR(pthread_mutex_destroy, m);
  int res = REAL(pthread_mutex_destroy)(m);
  if (res == 0) {
    MutexDestroy(cur_thread(), pc, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_mutex_lock, void *m) {
  SCOPED_INTERCEPTOR(pthread_mutex_lock, m);
  int res = REAL(pthread_mutex_lock)(m);
  if (res == 0) {
    MutexLock(cur_thread(), pc, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_mutex_trylock, void *m) {
  SCOPED_INTERCEPTOR(pthread_mutex_trylock, m);
  int res = REAL(pthread_mutex_trylock)(m);
  if (res == 0) {
    MutexLock(cur_thread(), pc, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_mutex_timedlock, void *m, void *abstime) {
  SCOPED_INTERCEPTOR(pthread_mutex_timedlock, m, abstime);
  int res = REAL(pthread_mutex_timedlock)(m, abstime);
  if (res == 0) {
    MutexLock(cur_thread(), pc, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_mutex_unlock, void *m) {
  SCOPED_INTERCEPTOR(pthread_mutex_unlock, m);
  MutexUnlock(cur_thread(), pc, (uptr)m);
  int res = REAL(pthread_mutex_unlock)(m);
  return res;
}

INTERCEPTOR(int, pthread_spin_init, void *m, int pshared) {
  SCOPED_INTERCEPTOR(pthread_spin_init, m, pshared);
  int res = REAL(pthread_spin_init)(m, pshared);
  if (res == 0) {
    MutexCreate(cur_thread(), pc, (uptr)m, false, false);
  }
  return res;
}

INTERCEPTOR(int, pthread_spin_destroy, void *m) {
  SCOPED_INTERCEPTOR(pthread_spin_destroy, m);
  int res = REAL(pthread_spin_destroy)(m);
  if (res == 0) {
    MutexDestroy(cur_thread(), pc, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_spin_lock, void *m) {
  SCOPED_INTERCEPTOR(pthread_spin_lock, m);
  int res = REAL(pthread_spin_lock)(m);
  if (res == 0) {
    MutexLock(cur_thread(), pc, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_spin_trylock, void *m) {
  SCOPED_INTERCEPTOR(pthread_spin_trylock, m);
  int res = REAL(pthread_spin_trylock)(m);
  if (res == 0) {
    MutexLock(cur_thread(), pc, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_spin_unlock, void *m) {
  SCOPED_INTERCEPTOR(pthread_spin_unlock, m);
  MutexUnlock(cur_thread(), pc, (uptr)m);
  int res = REAL(pthread_spin_unlock)(m);
  return res;
}

INTERCEPTOR(int, pthread_rwlock_init, void *m, void *a) {
  SCOPED_INTERCEPTOR(pthread_rwlock_init, m, a);
  int res = REAL(pthread_rwlock_init)(m, a);
  if (res == 0) {
    MutexCreate(cur_thread(), pc, (uptr)m, true, false);
  }
  return res;
}

INTERCEPTOR(int, pthread_rwlock_destroy, void *m) {
  SCOPED_INTERCEPTOR(pthread_rwlock_destroy, m);
  int res = REAL(pthread_rwlock_destroy)(m);
  if (res == 0) {
    MutexDestroy(cur_thread(), pc, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_rwlock_rdlock, void *m) {
  SCOPED_INTERCEPTOR(pthread_rwlock_rdlock, m);
  int res = REAL(pthread_rwlock_rdlock)(m);
  if (res == 0) {
    MutexReadLock(cur_thread(), pc, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_rwlock_tryrdlock, void *m) {
  SCOPED_INTERCEPTOR(pthread_rwlock_tryrdlock, m);
  int res = REAL(pthread_rwlock_tryrdlock)(m);
  if (res == 0) {
    MutexReadLock(cur_thread(), pc, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_rwlock_timedrdlock, void *m, void *abstime) {
  SCOPED_INTERCEPTOR(pthread_rwlock_timedrdlock, m, abstime);
  int res = REAL(pthread_rwlock_timedrdlock)(m, abstime);
  if (res == 0) {
    MutexReadLock(cur_thread(), pc, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_rwlock_wrlock, void *m) {
  SCOPED_INTERCEPTOR(pthread_rwlock_wrlock, m);
  int res = REAL(pthread_rwlock_wrlock)(m);
  if (res == 0) {
    MutexLock(cur_thread(), pc, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_rwlock_trywrlock, void *m) {
  SCOPED_INTERCEPTOR(pthread_rwlock_trywrlock, m);
  int res = REAL(pthread_rwlock_trywrlock)(m);
  if (res == 0) {
    MutexLock(cur_thread(), pc, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_rwlock_timedwrlock, void *m, void *abstime) {
  SCOPED_INTERCEPTOR(pthread_rwlock_timedwrlock, m, abstime);
  int res = REAL(pthread_rwlock_timedwrlock)(m, abstime);
  if (res == 0) {
    MutexLock(cur_thread(), pc, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_rwlock_unlock, void *m) {
  SCOPED_INTERCEPTOR(pthread_rwlock_unlock, m);
  MutexReadOrWriteUnlock(cur_thread(), pc, (uptr)m);
  int res = REAL(pthread_rwlock_unlock)(m);
  return res;
}

INTERCEPTOR(int, pthread_cond_init, void *c, void *a) {
  SCOPED_INTERCEPTOR(pthread_cond_init, c, a);
  int res = REAL(pthread_cond_init)(c, a);
  return res;
}

INTERCEPTOR(int, pthread_cond_destroy, void *c) {
  SCOPED_INTERCEPTOR(pthread_cond_destroy, c);
  int res = REAL(pthread_cond_destroy)(c);
  return res;
}

INTERCEPTOR(int, pthread_cond_signal, void *c) {
  SCOPED_INTERCEPTOR(pthread_cond_signal, c);
  int res = REAL(pthread_cond_signal)(c);
  return res;
}

INTERCEPTOR(int, pthread_cond_broadcast, void *c) {
  SCOPED_INTERCEPTOR(pthread_cond_broadcast, c);
  int res = REAL(pthread_cond_broadcast)(c);
  return res;
}

INTERCEPTOR(int, pthread_cond_wait, void *c, void *m) {
  SCOPED_INTERCEPTOR(pthread_cond_wait, c, m);
  MutexUnlock(cur_thread(), pc, (uptr)m);
  int res = REAL(pthread_cond_wait)(c, m);
  MutexLock(cur_thread(), pc, (uptr)m);
  return res;
}

INTERCEPTOR(int, pthread_cond_timedwait, void *c, void *m, void *abstime) {
  SCOPED_INTERCEPTOR(pthread_cond_timedwait, c, m, abstime);
  MutexUnlock(cur_thread(), pc, (uptr)m);
  int res = REAL(pthread_cond_timedwait)(c, m, abstime);
  MutexLock(cur_thread(), pc, (uptr)m);
  return res;
}

INTERCEPTOR(int, pthread_barrier_init, void *b, void *a, unsigned count) {
  SCOPED_INTERCEPTOR(pthread_barrier_init, b, a, count);
  int res = REAL(pthread_barrier_init)(b, a, count);
  return res;
}

INTERCEPTOR(int, pthread_barrier_destroy, void *b) {
  SCOPED_INTERCEPTOR(pthread_barrier_destroy, b);
  int res = REAL(pthread_barrier_destroy)(b);
  return res;
}

INTERCEPTOR(int, pthread_barrier_wait, void *b) {
  SCOPED_INTERCEPTOR(pthread_barrier_wait, b);
  Release(cur_thread(), pc, (uptr)b);
  int res = REAL(pthread_barrier_wait)(b);
  if (res == 0 || res == PTHREAD_BARRIER_SERIAL_THREAD) {
    Acquire(cur_thread(), pc, (uptr)b);
  }
  return res;
}

INTERCEPTOR(int, pthread_once, void *o, void (*f)()) {
  SCOPED_INTERCEPTOR(pthread_once, o, f);
  if (o == 0 || f == 0)
    return EINVAL;
  atomic_uint32_t *a = static_cast<atomic_uint32_t*>(o);
  u32 v = atomic_load(a, memory_order_acquire);
  if (v == 0 && atomic_compare_exchange_strong(a, &v, 1,
                                               memory_order_relaxed)) {
    (*f)();
    Release(cur_thread(), pc, (uptr)o);
    atomic_store(a, 2, memory_order_release);
  } else {
    while (v != 2) {
      pthread_yield();
      v = atomic_load(a, memory_order_acquire);
    }
    Acquire(cur_thread(), pc, (uptr)o);
  }
  return 0;
}

INTERCEPTOR(int, sem_init, void *s, int pshared, unsigned value) {
  SCOPED_INTERCEPTOR(sem_init, s, pshared, value);
  int res = REAL(sem_init)(s, pshared, value);
  return res;
}

INTERCEPTOR(int, sem_destroy, void *s) {
  SCOPED_INTERCEPTOR(sem_destroy, s);
  int res = REAL(sem_destroy)(s);
  return res;
}

INTERCEPTOR(int, sem_wait, void *s) {
  SCOPED_INTERCEPTOR(sem_wait, s);
  int res = REAL(sem_wait)(s);
  if (res == 0) {
    Acquire(cur_thread(), pc, (uptr)s);
  }
  return res;
}

INTERCEPTOR(int, sem_trywait, void *s) {
  SCOPED_INTERCEPTOR(sem_trywait, s);
  int res = REAL(sem_trywait)(s);
  if (res == 0) {
    Acquire(cur_thread(), pc, (uptr)s);
  }
  return res;
}

INTERCEPTOR(int, sem_timedwait, void *s, void *abstime) {
  SCOPED_INTERCEPTOR(sem_timedwait, s, abstime);
  int res = REAL(sem_timedwait)(s, abstime);
  if (res == 0) {
    Acquire(cur_thread(), pc, (uptr)s);
  }
  return res;
}

INTERCEPTOR(int, sem_post, void *s) {
  SCOPED_INTERCEPTOR(sem_post, s);
  Release(cur_thread(), pc, (uptr)s);
  int res = REAL(sem_post)(s);
  return res;
}

INTERCEPTOR(int, sem_getvalue, void *s, int *sval) {
  SCOPED_INTERCEPTOR(sem_getvalue, s, sval);
  int res = REAL(sem_getvalue)(s, sval);
  if (res == 0) {
    Acquire(cur_thread(), pc, (uptr)s);
  }
  return res;
}

INTERCEPTOR(long_t, read, int fd, void *buf, long_t sz) {
  SCOPED_INTERCEPTOR(read, fd, buf, sz);
  int res = REAL(read)(fd, buf, sz);
  if (res >= 0) {
    Acquire(cur_thread(), pc, fd2addr(fd));
  }
  return res;
}

INTERCEPTOR(long_t, pread, int fd, void *buf, long_t sz, unsigned off) {
  SCOPED_INTERCEPTOR(pread, fd, buf, sz, off);
  int res = REAL(pread)(fd, buf, sz, off);
  if (res >= 0) {
    Acquire(cur_thread(), pc, fd2addr(fd));
  }
  return res;
}

INTERCEPTOR(long_t, pread64, int fd, void *buf, long_t sz, u64 off) {
  SCOPED_INTERCEPTOR(pread64, fd, buf, sz, off);
  int res = REAL(pread64)(fd, buf, sz, off);
  if (res >= 0) {
    Acquire(cur_thread(), pc, fd2addr(fd));
  }
  return res;
}

INTERCEPTOR(long_t, readv, int fd, void *vec, int cnt) {
  SCOPED_INTERCEPTOR(readv, fd, vec, cnt);
  int res = REAL(readv)(fd, vec, cnt);
  if (res >= 0) {
    Acquire(cur_thread(), pc, fd2addr(fd));
  }
  return res;
}

INTERCEPTOR(long_t, preadv64, int fd, void *vec, int cnt, u64 off) {
  SCOPED_INTERCEPTOR(preadv64, fd, vec, cnt, off);
  int res = REAL(preadv64)(fd, vec, cnt, off);
  if (res >= 0) {
    Acquire(cur_thread(), pc, fd2addr(fd));
  }
  return res;
}

INTERCEPTOR(long_t, write, int fd, void *buf, long_t sz) {
  SCOPED_INTERCEPTOR(write, fd, buf, sz);
  Release(cur_thread(), pc, fd2addr(fd));
  int res = REAL(write)(fd, buf, sz);
  return res;
}

INTERCEPTOR(long_t, pwrite, int fd, void *buf, long_t sz, unsigned off) {
  SCOPED_INTERCEPTOR(pwrite, fd, buf, sz, off);
  Release(cur_thread(), pc, fd2addr(fd));
  int res = REAL(pwrite)(fd, buf, sz, off);
  return res;
}

INTERCEPTOR(long_t, pwrite64, int fd, void *buf, long_t sz, unsigned off) {
  SCOPED_INTERCEPTOR(pwrite64, fd, buf, sz, off);
  Release(cur_thread(), pc, fd2addr(fd));
  int res = REAL(pwrite64)(fd, buf, sz, off);
  return res;
}

INTERCEPTOR(long_t, writev, int fd, void *vec, int cnt) {
  SCOPED_INTERCEPTOR(writev, fd, vec, cnt);
  Release(cur_thread(), pc, fd2addr(fd));
  int res = REAL(writev)(fd, vec, cnt);
  return res;
}

INTERCEPTOR(long_t, pwritev64, int fd, void *vec, int cnt, u64 off) {
  SCOPED_INTERCEPTOR(pwritev64, fd, vec, cnt, off);
  Release(cur_thread(), pc, fd2addr(fd));
  int res = REAL(pwritev64)(fd, vec, cnt, off);
  return res;
}

INTERCEPTOR(long_t, send, int fd, void *buf, long_t len, int flags) {
  SCOPED_INTERCEPTOR(send, fd, buf, len, flags);
  Release(cur_thread(), pc, fd2addr(fd));
  int res = REAL(send)(fd, buf, len, flags);
  return res;
}

INTERCEPTOR(long_t, sendmsg, int fd, void *msg, int flags) {
  SCOPED_INTERCEPTOR(sendmsg, fd, msg, flags);
  Release(cur_thread(), pc, fd2addr(fd));
  int res = REAL(sendmsg)(fd, msg, flags);
  return res;
}

INTERCEPTOR(long_t, recv, int fd, void *buf, long_t len, int flags) {
  SCOPED_INTERCEPTOR(recv, fd, buf, len, flags);
  int res = REAL(recv)(fd, buf, len, flags);
  if (res >= 0) {
    Acquire(cur_thread(), pc, fd2addr(fd));
  }
  return res;
}

INTERCEPTOR(long_t, recvmsg, int fd, void *msg, int flags) {
  SCOPED_INTERCEPTOR(recvmsg, fd, msg, flags);
  int res = REAL(recvmsg)(fd, msg, flags);
  if (res >= 0) {
    Acquire(cur_thread(), pc, fd2addr(fd));
  }
  return res;
}

INTERCEPTOR(int, unlink, char *path) {
  SCOPED_INTERCEPTOR(unlink, path);
  Release(cur_thread(), pc, file2addr(path));
  int res = REAL(unlink)(path);
  return res;
}

INTERCEPTOR(void*, fopen, char *path, char *mode) {
  SCOPED_INTERCEPTOR(fopen, path, mode);
  void *res = REAL(fopen)(path, mode);
  Acquire(cur_thread(), pc, file2addr(path));
  return res;
}

INTERCEPTOR(int, rmdir, char *path) {
  SCOPED_INTERCEPTOR(rmdir, path);
  Release(cur_thread(), pc, dir2addr(path));
  int res = REAL(rmdir)(path);
  return res;
}

INTERCEPTOR(void*, opendir, char *path) {
  SCOPED_INTERCEPTOR(opendir, path);
  void *res = REAL(opendir)(path);
  Acquire(cur_thread(), pc, dir2addr(path));
  return res;
}

INTERCEPTOR(int, epoll_ctl, int epfd, int op, int fd, void *ev) {
  SCOPED_INTERCEPTOR(epoll_ctl, epfd, op, fd, ev);
  if (op == EPOLL_CTL_ADD) {
    Release(cur_thread(), pc, epollfd2addr(epfd));
  }
  int res = REAL(epoll_ctl)(epfd, op, fd, ev);
  return res;
}

INTERCEPTOR(int, epoll_wait, int epfd, void *ev, int cnt, int timeout) {
  SCOPED_INTERCEPTOR(epoll_wait, epfd, ev, cnt, timeout);
  int res = REAL(epoll_wait)(epfd, ev, cnt, timeout);
  if (res > 0) {
    Acquire(cur_thread(), pc, epollfd2addr(epfd));
  }
  return res;
}

namespace __tsan {

void InitializeInterceptors() {
  if (atexit(&finalize)) {
    Printf("ThreadSanitizer: failed to setup atexit callback\n");
    Die();
  }

  if (pthread_key_create(&g_thread_finalize_key, &thread_finalize)) {
    Printf("ThreadSanitizer: failed to create thread key\n");
    Die();
  }

  INTERCEPT_FUNCTION(malloc);
  INTERCEPT_FUNCTION(calloc);
  INTERCEPT_FUNCTION(realloc);
  INTERCEPT_FUNCTION(free);
  INTERCEPT_FUNCTION(cfree);
  INTERCEPT_FUNCTION(mmap);
  INTERCEPT_FUNCTION(mmap64);
  INTERCEPT_FUNCTION(munmap);

  INTERCEPT_FUNCTION(_Znwm);
  INTERCEPT_FUNCTION(_ZnwmRKSt9nothrow_t);
  INTERCEPT_FUNCTION(_Znam);
  INTERCEPT_FUNCTION(_ZnamRKSt9nothrow_t);
  INTERCEPT_FUNCTION(_ZdlPv);
  INTERCEPT_FUNCTION(_ZdlPvRKSt9nothrow_t);
  INTERCEPT_FUNCTION(_ZdaPv);
  INTERCEPT_FUNCTION(_ZdaPvRKSt9nothrow_t);

  INTERCEPT_FUNCTION(strlen);
  INTERCEPT_FUNCTION(memset);
  INTERCEPT_FUNCTION(memcpy);
  INTERCEPT_FUNCTION(strcmp);
  INTERCEPT_FUNCTION(memchr);
  INTERCEPT_FUNCTION(memrchr);
  INTERCEPT_FUNCTION(memmove);
  INTERCEPT_FUNCTION(memcmp);
  INTERCEPT_FUNCTION(strchr);
  INTERCEPT_FUNCTION(strchrnul);
  INTERCEPT_FUNCTION(strrchr);
  INTERCEPT_FUNCTION(strncmp);
  INTERCEPT_FUNCTION(strcpy);  // NOLINT
  INTERCEPT_FUNCTION(strncpy);

  INTERCEPT_FUNCTION(pthread_create);
  INTERCEPT_FUNCTION(pthread_join);
  INTERCEPT_FUNCTION(pthread_detach);

  INTERCEPT_FUNCTION(pthread_mutex_init);
  INTERCEPT_FUNCTION(pthread_mutex_destroy);
  INTERCEPT_FUNCTION(pthread_mutex_lock);
  INTERCEPT_FUNCTION(pthread_mutex_trylock);
  INTERCEPT_FUNCTION(pthread_mutex_timedlock);
  INTERCEPT_FUNCTION(pthread_mutex_unlock);

  INTERCEPT_FUNCTION(pthread_spin_init);
  INTERCEPT_FUNCTION(pthread_spin_destroy);
  INTERCEPT_FUNCTION(pthread_spin_lock);
  INTERCEPT_FUNCTION(pthread_spin_trylock);
  INTERCEPT_FUNCTION(pthread_spin_unlock);

  INTERCEPT_FUNCTION(pthread_rwlock_init);
  INTERCEPT_FUNCTION(pthread_rwlock_destroy);
  INTERCEPT_FUNCTION(pthread_rwlock_rdlock);
  INTERCEPT_FUNCTION(pthread_rwlock_tryrdlock);
  INTERCEPT_FUNCTION(pthread_rwlock_timedrdlock);
  INTERCEPT_FUNCTION(pthread_rwlock_wrlock);
  INTERCEPT_FUNCTION(pthread_rwlock_trywrlock);
  INTERCEPT_FUNCTION(pthread_rwlock_timedwrlock);
  INTERCEPT_FUNCTION(pthread_rwlock_unlock);

  INTERCEPT_FUNCTION(pthread_cond_init);
  INTERCEPT_FUNCTION(pthread_cond_destroy);
  INTERCEPT_FUNCTION(pthread_cond_signal);
  INTERCEPT_FUNCTION(pthread_cond_broadcast);
  INTERCEPT_FUNCTION(pthread_cond_wait);
  INTERCEPT_FUNCTION(pthread_cond_timedwait);

  INTERCEPT_FUNCTION(pthread_barrier_init);
  INTERCEPT_FUNCTION(pthread_barrier_destroy);
  INTERCEPT_FUNCTION(pthread_barrier_wait);

  INTERCEPT_FUNCTION(pthread_once);

  INTERCEPT_FUNCTION(sem_init);
  INTERCEPT_FUNCTION(sem_destroy);
  INTERCEPT_FUNCTION(sem_wait);
  INTERCEPT_FUNCTION(sem_trywait);
  INTERCEPT_FUNCTION(sem_timedwait);
  INTERCEPT_FUNCTION(sem_post);
  INTERCEPT_FUNCTION(sem_getvalue);

  INTERCEPT_FUNCTION(read);
  INTERCEPT_FUNCTION(pread);
  INTERCEPT_FUNCTION(pread64);
  INTERCEPT_FUNCTION(readv);
  INTERCEPT_FUNCTION(preadv64);
  INTERCEPT_FUNCTION(write);
  INTERCEPT_FUNCTION(pwrite);
  INTERCEPT_FUNCTION(pwrite64);
  INTERCEPT_FUNCTION(writev);
  INTERCEPT_FUNCTION(pwritev64);
  INTERCEPT_FUNCTION(send);
  INTERCEPT_FUNCTION(sendmsg);
  INTERCEPT_FUNCTION(recv);
  INTERCEPT_FUNCTION(recvmsg);

  INTERCEPT_FUNCTION(unlink);
  INTERCEPT_FUNCTION(fopen);
  INTERCEPT_FUNCTION(rmdir);
  INTERCEPT_FUNCTION(opendir);

  INTERCEPT_FUNCTION(epoll_ctl);
  INTERCEPT_FUNCTION(epoll_wait);
}

void internal_memset(void *ptr, int c, uptr size) {
  REAL(memset)(ptr, c, size);
}

void internal_memcpy(void *dst, const void *src, uptr size) {
  REAL(memcpy)(dst, src, size);
}

int internal_strcmp(const char *s1, const char *s2) {
  return REAL(strcmp)((signed char*)s1, (signed char*)s2);
}

}  // namespace __tsan
