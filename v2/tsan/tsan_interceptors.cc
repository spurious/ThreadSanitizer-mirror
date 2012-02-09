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

#include "tsan_interceptors.h"

// FIXME: This is temporary. We need to factor out the interception machinery
// from AddressSanitizer.
#include <dlfcn.h>

extern "C"
int WRAP(pthread_create)(void *t, void *attr, void *callback, void *param) {
  // __tsan::Printf("pthread_create %p\n", t);
  typedef int (*pthread_create_f)(void*, void*, void*, void*);
  pthread_create_f orig_pthread_create =
    (pthread_create_f) dlsym(RTLD_NEXT, "pthread_create");

  int res = ORIG(pthread_create)(t, attr, callback, param);
  return res;
}
