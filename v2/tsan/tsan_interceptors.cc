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

using __tsan::cur_thread;
using __tsan::uptr;
using __tsan::atomic_uintptr_t;
using __tsan::atomic_store;
using __tsan::atomic_load;
using __tsan::memory_order_relaxed;
using __tsan::memory_order_release;
using __tsan::memory_order_acquire;
using __tsan::ThreadCreate;
using __tsan::ThreadStart;
using __tsan::ThreadFinish;
using __tsan::ThreadDetach;
using __tsan::ThreadJoin;

extern "C" int pthread_attr_getdetachstate(void *attr, int *v);
extern "C" int pthread_yield();

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
  while ((tid = atomic_load(&p->tid, memory_order_acquire)) == 0)
    pthread_yield();
  atomic_store(&p->tid, 0, memory_order_release);
  ThreadStart(cur_thread(), tid);
  void *res = callback(param);
  ThreadFinish(cur_thread());
  return res;
}

INTERCEPTOR(int, pthread_create,
    void *th, void *attr, void *(*callback)(void*), void * param) {
  __tsan_init();
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
  __tsan_init();
  int res = REAL(pthread_join)(th, ret);
  if (res == 0) {
    ThreadJoin(cur_thread(), (uptr)th);
  }
  return res;
}

INTERCEPTOR(int, pthread_detach, void *th) {
  __tsan_init();
  int res = REAL(pthread_detach)(th);
  if (res == 0) {
    ThreadDetach(cur_thread(), (uptr)th);
  }
  return res;
}

INTERCEPTOR(int, pthread_mutex_init, void *m, const void *a) {
  __tsan_init();
  int res = REAL(pthread_mutex_init)(m, a);
  if (res == 0) {
    MutexCreate(cur_thread(), CALLERPC, (uptr)m, false);
  }
  return res;
}

INTERCEPTOR(int, pthread_mutex_destroy, void *m) {
  int res = REAL(pthread_mutex_destroy)(m);
  if (res == 0) {
    MutexDestroy(cur_thread(), CALLERPC, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_mutex_lock, void *m) {
  __tsan_init();
  int res = REAL(pthread_mutex_lock)(m);
  if (res == 0) {
    MutexLock(cur_thread(), CALLERPC, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_mutex_trylock, void *m) {
  __tsan_init();
  int res = REAL(pthread_mutex_trylock)(m);
  if (res == 0) {
    MutexLock(cur_thread(), CALLERPC, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_mutex_unlock, void *m) {
  MutexUnlock(cur_thread(), CALLERPC, (uptr)m);
  int res = REAL(pthread_mutex_unlock)(m);
  return res;
}

INTERCEPTOR(int, pthread_spin_init, void *m, int pshared) {
  __tsan_init();
  int res = REAL(pthread_spin_init)(m, pshared);
  if (res == 0) {
    MutexCreate(cur_thread(), CALLERPC, (uptr)m, false);
  }
  return res;
}

INTERCEPTOR(int, pthread_spin_destroy, void *m) {
  int res = REAL(pthread_spin_destroy)(m);
  if (res == 0) {
    MutexDestroy(cur_thread(), CALLERPC, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_spin_lock, void *m) {
  __tsan_init();
  int res = REAL(pthread_spin_lock)(m);
  if (res == 0) {
    MutexLock(cur_thread(), CALLERPC, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_spin_trylock, void *m) {
  __tsan_init();
  int res = REAL(pthread_spin_trylock)(m);
  if (res == 0) {
    MutexLock(cur_thread(), CALLERPC, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_spin_unlock, void *m) {
  MutexUnlock(cur_thread(), CALLERPC, (uptr)m);
  int res = REAL(pthread_spin_unlock)(m);
  return res;
}

namespace __tsan {

void InitializeInterceptors() {
  INTERCEPT_FUNCTION(pthread_create);
  INTERCEPT_FUNCTION(pthread_join);
  INTERCEPT_FUNCTION(pthread_detach);
  INTERCEPT_FUNCTION(pthread_mutex_init);
  INTERCEPT_FUNCTION(pthread_mutex_destroy);
  INTERCEPT_FUNCTION(pthread_mutex_lock);
  INTERCEPT_FUNCTION(pthread_mutex_trylock);
  INTERCEPT_FUNCTION(pthread_mutex_unlock);
  INTERCEPT_FUNCTION(pthread_spin_init);
  INTERCEPT_FUNCTION(pthread_spin_destroy);
  INTERCEPT_FUNCTION(pthread_spin_lock);
  INTERCEPT_FUNCTION(pthread_spin_trylock);
  INTERCEPT_FUNCTION(pthread_spin_unlock);
}

}  // namespace __tsan
