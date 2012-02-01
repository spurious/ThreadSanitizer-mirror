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
#include <stdint.h>

MemLoc::MemLoc(int offset_from_aligned) {
  static uintptr_t uniq = 0x20000;  // something far enough from 0 and 8-aligned
  loc_  = (void*)(__sync_fetch_and_add(&uniq, 8) + offset_from_aligned);
  fprintf(stderr, "MemLoc: %p\n", loc_);
}

MemLoc::~MemLoc() { }

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

void ScopedThread::Access(const MemLoc &ml, bool is_write,
                          int size, bool expect_race) {
  fprintf(stderr, "%s: %p %d \n", is_write ? "WRITE" : "READ ", ml.loc(), size);
}
