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

INTERCEPTOR(int, pthread_create,
    void *t, void *attr, void *callback, void *param) {
  // __tsan::Printf("pthread_create %p\n", t);
  int res = REAL(pthread_create)(t, attr, callback, param);
  return res;
}

namespace __tsan {
void InitializeInterceptors() {
  INTERCEPT_FUNCTION(pthread_create);
}
}  // namespace __tsan

