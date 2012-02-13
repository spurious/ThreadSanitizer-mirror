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
#include "tsan_thread.h"
#include <pthread.h>

using __tsan::Thread;
using __tsan::cur_thread;
using __tsan::uptr;

static void *tsan_thread_start(void *arg) {
  Thread *t = (Thread*)arg;
  // __tsan::Printf("tsan_thread_start %p\n", t);
  return t->ThreadStart();
}

INTERCEPTOR(int, pthread_create,
    pthread_t *__restrict th, const pthread_attr_t *__restrict attr,
    void *(*callback)(void*), void *__restrict param) {
  __tsan_init();
  Thread *t = Thread::Create(callback, param);
  // __tsan::Printf("pthread_create %p\n", t);
  int res = REAL(pthread_create)(th, attr, tsan_thread_start, t);
  return res;
}

INTERCEPTOR(int, pthread_mutex_init,
            pthread_mutex_t *m, const pthread_mutexattr_t *a) {
  __tsan_init();
  int res = REAL(pthread_mutex_init)(m, a);
  if (res == 0) {
    MutexCreate(&cur_thread, (uptr)m, false);
  }
  return res;
}

INTERCEPTOR(int, pthread_mutex_destroy,
            pthread_mutex_t *m) {
  int res = REAL(pthread_mutex_destroy)(m);
  if (res == 0) {
    MutexDestroy(&cur_thread, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_mutex_lock,
            pthread_mutex_t *m) {
  __tsan_init();
  int res = REAL(pthread_mutex_lock)(m);
  if (res == 0) {
    MutexLock(&cur_thread, (uptr)m);
  }
  return res;
}

INTERCEPTOR(int, pthread_mutex_unlock,
            pthread_mutex_t *m) {
  MutexUnlock(&cur_thread, (uptr)m);
  int res = REAL(pthread_mutex_unlock)(m);
  return res;
}

namespace __tsan {

void InitializeInterceptors() {
  INTERCEPT_FUNCTION(pthread_create);
  INTERCEPT_FUNCTION(pthread_mutex_init);
  INTERCEPT_FUNCTION(pthread_mutex_destroy);
  INTERCEPT_FUNCTION(pthread_mutex_lock);
  INTERCEPT_FUNCTION(pthread_mutex_unlock);
}

}  // namespace __tsan
