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

#define CALLERPC ((__tsan::uptr)__builtin_return_address(0))

using namespace __tsan;  // NOLINT

extern "C" int pthread_attr_getdetachstate(void *attr, int *v);
extern "C" int pthread_key_create(unsigned *key, void (*destructor)(void* v));
extern "C" int pthread_setspecific(unsigned key, const void *v);
extern "C" int pthread_yield();
extern "C" void *__libc_malloc(uptr size);
extern "C" void *__libc_calloc(uptr nmemb, uptr size);
extern "C" void __libc_free(void *ptr);
extern "C" void *__libc_realloc(void *ptr, uptr size);
extern "C" int atexit(void (*function)());
extern "C" void _exit(int status);

static unsigned g_thread_finalize_key;

struct ScopedInterceptor {
  ScopedInterceptor() {
    __tsan_init();
  }

  ~ScopedInterceptor() {
  }
};

static void finalize() {
  int status = Finalize(cur_thread());
  if (status)
    _exit(status);
}

extern "C" void *malloc(uptr size) {
  ScopedInterceptor si;
  void *p = __libc_malloc(size);
  return p;
}

extern "C" void *calloc(uptr size, uptr n) {
  ScopedInterceptor si;
  void *p = __libc_calloc(size, n);
  return p;
}

extern "C" void *realloc(void *p, uptr size) {
  ScopedInterceptor si;
  void *p2 = __libc_realloc(p, size);
  return p2;
}

extern "C" void free(void *p) {
  ScopedInterceptor si;
  __libc_free(p);
}

extern "C" void cfree(void *p) {
  ScopedInterceptor si;
  __libc_free(p);
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
  ScopedInterceptor si;
  int detached = 0;
  if (attr)
    pthread_attr_getdetachstate(attr, &detached);
  ThreadParam p;
  p.callback = callback;
  p.param = param;
  atomic_store(&p.tid, 0, memory_order_relaxed);
  int res = REAL(pthread_create)(th, attr, tsan_thread_start, &p);
  if (res == 0) {
    int tid = ThreadCreate(cur_thread(), *(uptr*)th, detached);
    CHECK_NE(tid, 0);
    atomic_store(&p.tid, tid, memory_order_release);
    while (atomic_load(&p.tid, memory_order_acquire) != 0)
      pthread_yield();
  }
  return res;
}

INTERCEPTOR(int, pthread_join, void *th, void **ret) {
  ScopedInterceptor si;
  int res = REAL(pthread_join)(th, ret);
  if (res == 0) {
    ThreadJoin(cur_thread(), (uptr)th);
  }
  return res;
}

INTERCEPTOR(int, pthread_detach, void *th) {
  ScopedInterceptor si;
  int res = REAL(pthread_detach)(th);
  if (res == 0) {
    ThreadDetach(cur_thread(), (uptr)th);
  }
  return res;
}

INTERCEPTOR(int, pthread_mutex_init, void *m, const void *a) {
  ScopedInterceptor si;
  int res = REAL(pthread_mutex_init)(m, a);
  if (res == 0) {
    MutexCreate(cur_thread(), CALLERPC, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_mutex_destroy, void *m) {
  ScopedInterceptor si;
  int res = REAL(pthread_mutex_destroy)(m);
  if (res == 0) {
    MutexDestroy(cur_thread(), CALLERPC, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_mutex_lock, void *m) {
  ScopedInterceptor si;
  int res = REAL(pthread_mutex_lock)(m);
  if (res == 0) {
    MutexLock(cur_thread(), CALLERPC, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_mutex_trylock, void *m) {
  ScopedInterceptor si;
  int res = REAL(pthread_mutex_trylock)(m);
  if (res == 0) {
    MutexLock(cur_thread(), CALLERPC, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_mutex_timedlock, void *m, void *abstime) {
  ScopedInterceptor si;
  int res = REAL(pthread_mutex_timedlock)(m, abstime);
  if (res == 0) {
    MutexLock(cur_thread(), CALLERPC, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_mutex_unlock, void *m) {
  ScopedInterceptor si;
  MutexUnlock(cur_thread(), CALLERPC, (uptr)m);
  int res = REAL(pthread_mutex_unlock)(m);
  return res;
}

INTERCEPTOR(int, pthread_spin_init, void *m, int pshared) {
  ScopedInterceptor si;
  int res = REAL(pthread_spin_init)(m, pshared);
  if (res == 0) {
    MutexCreate(cur_thread(), CALLERPC, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_spin_destroy, void *m) {
  ScopedInterceptor si;
  int res = REAL(pthread_spin_destroy)(m);
  if (res == 0) {
    MutexDestroy(cur_thread(), CALLERPC, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_spin_lock, void *m) {
  ScopedInterceptor si;
  int res = REAL(pthread_spin_lock)(m);
  if (res == 0) {
    MutexLock(cur_thread(), CALLERPC, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_spin_trylock, void *m) {
  ScopedInterceptor si;
  int res = REAL(pthread_spin_trylock)(m);
  if (res == 0) {
    MutexLock(cur_thread(), CALLERPC, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_spin_unlock, void *m) {
  ScopedInterceptor si;
  MutexUnlock(cur_thread(), CALLERPC, (uptr)m);
  int res = REAL(pthread_spin_unlock)(m);
  return res;
}

INTERCEPTOR(int, pthread_rwlock_init, void *m, void *a) {
  ScopedInterceptor si;
  int res = REAL(pthread_rwlock_init)(m, a);
  if (res == 0) {
    MutexCreate(cur_thread(), CALLERPC, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_rwlock_destroy, void *m) {
  ScopedInterceptor si;
  int res = REAL(pthread_rwlock_destroy)(m);
  if (res == 0) {
    MutexDestroy(cur_thread(), CALLERPC, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_rwlock_rdlock, void *m) {
  ScopedInterceptor si;
  int res = REAL(pthread_rwlock_rdlock)(m);
  if (res == 0) {
    MutexReadLock(cur_thread(), CALLERPC, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_rwlock_tryrdlock, void *m) {
  ScopedInterceptor si;
  int res = REAL(pthread_rwlock_tryrdlock)(m);
  if (res == 0) {
    MutexReadLock(cur_thread(), CALLERPC, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_rwlock_timedrdlock, void *m, void *abstime) {
  ScopedInterceptor si;
  int res = REAL(pthread_rwlock_timedrdlock)(m, abstime);
  if (res == 0) {
    MutexReadLock(cur_thread(), CALLERPC, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_rwlock_wrlock, void *m) {
  ScopedInterceptor si;
  int res = REAL(pthread_rwlock_wrlock)(m);
  if (res == 0) {
    MutexLock(cur_thread(), CALLERPC, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_rwlock_trywrlock, void *m) {
  ScopedInterceptor si;
  int res = REAL(pthread_rwlock_trywrlock)(m);
  if (res == 0) {
    MutexLock(cur_thread(), CALLERPC, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_rwlock_timedwrlock, void *m, void *abstime) {
  ScopedInterceptor si;
  int res = REAL(pthread_rwlock_timedwrlock)(m, abstime);
  if (res == 0) {
    MutexLock(cur_thread(), CALLERPC, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_rwlock_unlock, void *m) {
  ScopedInterceptor si;
  MutexReadOrWriteUnlock(cur_thread(), CALLERPC, (uptr)m);
  int res = REAL(pthread_rwlock_unlock)(m);
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
}

}  // namespace __tsan
