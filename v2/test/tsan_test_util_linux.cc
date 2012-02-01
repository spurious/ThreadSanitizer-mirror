//===-- tsan_test_util_linux.cc ---------------------------------*- C++ -*-===//
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
// Test utils, linux implementation.
//===----------------------------------------------------------------------===//

#include "tsan_test_util.h"

#include <pthread.h>
#include <stdio.h>

struct ScopedThread::Impl {
  pthread_t thread;
};

static void *ScopedThreadCallback(void *arg) {
  fprintf(stderr, "ScopedThreadCallback: %p\n", arg);
  return NULL;
}

ScopedThread::ScopedThread() {
  impl_ = new Impl;
  pthread_create(&impl_->thread, NULL, ScopedThreadCallback, impl_);
}

ScopedThread::~ScopedThread() {
  pthread_join(impl_->thread, NULL);
  delete impl_;
}
