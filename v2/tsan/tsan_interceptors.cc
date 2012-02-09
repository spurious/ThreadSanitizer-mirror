//===-- tsan_interceptors.cc ------------------------------------*- C++ -*-===//
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
// Platform-independent code for interceptors.
// Do not put any platform-specific includes/ifdefs here.
//===----------------------------------------------------------------------===//

#include "interception/interception.h"
#include "tsan_rtl.h"
#include "tsan_thread.h"

using __tsan::Thread;

static void *tsan_thread_start(void *arg) {
  Thread *t = (Thread*)arg;
  return t->ThreadStart();
}

INTERCEPTOR(int, pthread_create,
    void *th, void *attr, void *callback, void *param) {
  // __tsan::Printf("pthread_create %p\n", t);
  Thread *t = Thread::Create(callback, param);
  int res = REAL(pthread_create)(th, attr, (void*)tsan_thread_start, t);
  return res;
}

namespace __tsan {
void InitializeInterceptors() {
  INTERCEPT_FUNCTION(pthread_create);
}
}  // namespace __tsan
