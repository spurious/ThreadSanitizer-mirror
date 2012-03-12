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

using namespace __tsan;  // NOLINT

extern "C" int pthread_attr_getdetachstate(void *attr, int *v);
extern "C" int pthread_key_create(unsigned *key, void (*destructor)(void* v));
extern "C" int pthread_setspecific(unsigned key, const void *v);
extern "C" int pthread_mutexattr_gettype(void *a, int *type);
extern "C" int pthread_yield();
extern "C" void *__libc_malloc(uptr size);
extern "C" void *__libc_calloc(uptr nmemb, uptr size);
extern "C" void __libc_free(void *ptr);
extern "C" void *__libc_realloc(void *ptr, uptr size);
extern "C" int atexit(void (*function)());
extern "C" void _exit(int status);
const int PTHREAD_MUTEX_RECURSIVE = 1;
const int PTHREAD_MUTEX_RECURSIVE_NP = 1;

static unsigned g_thread_finalize_key;

static __thread int thread_in_rtl;

class ScopedInterceptor {
 public:
  ScopedInterceptor(ThreadState *thr, uptr pc)
    : thr_(thr) {
    thread_in_rtl++;
    if (thread_in_rtl == 1) {
      Initialize(thr);
      FuncEntry(thr, pc);
    }
  }

  ~ScopedInterceptor() {
    if (thread_in_rtl == 1) {
      FuncExit(thr_);
    }
    thread_in_rtl--;
  }
 private:
  ThreadState *const thr_;
};

#define SCOPED_INTERCEPTOR_RAW(func, ...) \
    ScopedInterceptor si(cur_thread(), \
    (__tsan::uptr)__builtin_return_address(0)); \
    const uptr pc = (uptr)func; \
    (void)pc; \
/**/

#define SCOPED_INTERCEPTOR(func, ...) \
    SCOPED_INTERCEPTOR_RAW(func, __VA_ARGS__); \
    if (thread_in_rtl > 1) \
      return REAL(func)(__VA_ARGS__); \
/**/

static void finalize() {
  int status = Finalize(cur_thread());
  if (status)
    _exit(status);
}

INTERCEPTOR(void*, malloc, uptr size) {
  SCOPED_INTERCEPTOR_RAW(malloc, size);
  if (thread_in_rtl > 1)
    return __libc_malloc(size);
  void *p = __libc_malloc(size);
  return p;
}

INTERCEPTOR(void*, calloc, uptr size, uptr n) {
  SCOPED_INTERCEPTOR_RAW(calloc, size, n);
  if (thread_in_rtl > 1)
    __libc_calloc(size, n);
  void *p = __libc_calloc(size, n);
  return p;
}

INTERCEPTOR(void*, realloc, void *p, uptr size) {
  SCOPED_INTERCEPTOR_RAW(realloc, p, size);
  if (thread_in_rtl > 1)
    return __libc_realloc(p, size);
  void *p2 = __libc_realloc(p, size);
  return p2;
}

INTERCEPTOR(void, free, void *p) {
  SCOPED_INTERCEPTOR_RAW(free, p);
  if (thread_in_rtl > 1)
    return __libc_free(p);
  __libc_free(p);
}

INTERCEPTOR(void, cfree, void *p) {
  SCOPED_INTERCEPTOR_RAW(cfree, p);
  if (thread_in_rtl > 1)
    return __libc_free(p);
  __libc_free(p);
}

INTERCEPTOR(void*, memset, void *dst, int v, uptr size) {
  SCOPED_INTERCEPTOR(memset, dst, v, size);
  MemoryAccessRange(cur_thread(), pc, (uptr)dst, size, true);
  return REAL(memset)(dst, v, size);
}

INTERCEPTOR(void*, memcpy, void *dst, const void *src, uptr size) {
  SCOPED_INTERCEPTOR(memcpy, dst, src, size);
  MemoryAccessRange(cur_thread(), pc, (uptr)dst, size, true);
  MemoryAccessRange(cur_thread(), pc, (uptr)src, size, false);
  return REAL(memcpy)(dst, src, size);
}

/*
void *operator new(unsigned long size) {
  SCOPED_INTERCEPTOR(malloc);
  void *p = __libc_malloc(size);
  return p;
}

void *operator new[](unsigned long size) {
  SCOPED_INTERCEPTOR(malloc);
  void *p = __libc_malloc(size);
  return p;
}

void operator delete(void *p) {
  SCOPED_INTERCEPTOR(free);
  __libc_free(p);
}

void operator delete[](void *p) {
  SCOPED_INTERCEPTOR(free);
  __libc_free(p);
}
*/

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

static void *tsan_thread_start(void *arg) {
  ThreadParam *p = (ThreadParam*)arg;
  void* (*callback)(void *arg) = p->callback;
  void *param = p->param;
  int tid = 0;
  if (pthread_setspecific(g_thread_finalize_key, (void*)4)) {
    Printf("ThreadSanitizer: failed to set thread key\n");
    Die();
  }
  while ((tid = atomic_load(&p->tid, memory_order_acquire)) == 0)
    pthread_yield();
  atomic_store(&p->tid, 0, memory_order_release);
  ThreadStart(cur_thread(), tid);
  void *res = callback(param);
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
  int res = REAL(pthread_create)(th, attr, tsan_thread_start, &p);
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
  int res = pthread_cond_init(c, a);
  return res;
}

INTERCEPTOR(int, pthread_cond_destroy, void *c) {
  SCOPED_INTERCEPTOR(pthread_cond_destroy, c);
  int res = pthread_cond_destroy(c);
  return res;
}

INTERCEPTOR(int, pthread_cond_signal, void *c) {
  SCOPED_INTERCEPTOR(pthread_cond_signal, c);
  int res = pthread_cond_signal(c);
  return res;
}

INTERCEPTOR(int, pthread_cond_broadcast, void *c) {
  SCOPED_INTERCEPTOR(pthread_cond_broadcast, c);
  int res = pthread_cond_broadcast(c);
  return res;
}

INTERCEPTOR(int, pthread_cond_wait, void *c, void *m) {
  SCOPED_INTERCEPTOR(pthread_cond_wait, c, m);
  MutexUnlock(cur_thread(), pc, (uptr)m);
  int res = pthread_cond_wait(c, m);
  MutexLock(cur_thread(), pc, (uptr)m);
  return res;
}

INTERCEPTOR(int, pthread_cond_timedwait, void *c, void *m, void *abstime) {
  SCOPED_INTERCEPTOR(pthread_cond_timedwait, c, m, abstime);
  MutexUnlock(cur_thread(), pc, (uptr)m);
  int res = pthread_cond_timedwait(c, m, abstime);
  MutexLock(cur_thread(), pc, (uptr)m);
  return res;
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

static uptr fd2addr(int fd) {
  (void)fd;
  static int fdaddr;
  return (uptr)&fdaddr;
}

INTERCEPTOR(long, read, int fd, void *buf, long sz) {
  SCOPED_INTERCEPTOR(read, fd, buf, sz);
  int res = REAL(read)(fd, buf, sz);
  if (res >= 0) {
    Acquire(cur_thread(), pc, fd2addr(fd));
  }
  return res;
}

INTERCEPTOR(long, write, int fd, void *buf, long sz) {
  SCOPED_INTERCEPTOR(write, fd, buf, sz);
  Release(cur_thread(), pc, fd2addr(fd));
  int res = REAL(write)(fd, buf, sz);
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

  INTERCEPT_FUNCTION(memset);
  INTERCEPT_FUNCTION(memcpy);

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

  INTERCEPT_FUNCTION(sem_init);
  INTERCEPT_FUNCTION(sem_destroy);
  INTERCEPT_FUNCTION(sem_wait);
  INTERCEPT_FUNCTION(sem_trywait);
  INTERCEPT_FUNCTION(sem_timedwait);
  INTERCEPT_FUNCTION(sem_post);
  INTERCEPT_FUNCTION(sem_getvalue);

  INTERCEPT_FUNCTION(read);
  INTERCEPT_FUNCTION(write);
}

void internal_memset(void *ptr, int c, uptr size) {
  REAL(memset)(ptr, c, size);
}

void internal_memcpy(void *dst, const void *src, uptr size) {
  REAL(memcpy)(dst, src, size);
}

}  // namespace __tsan
