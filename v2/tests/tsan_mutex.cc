//===-- tsan_mutex.cc -------------------------------------------*- C++ -*-===//
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
#include "tsan_interface.h"
#include "tsan_test_util.h"
#include "gtest/gtest.h"

TEST(ThreadSanitizer, SimpleMutex) {
  ScopedThread t;
  Mutex m;
  t.Create(m);
  t.Lock(m);
  t.Unlock(m);
  t.Destroy(m);
}

TEST(ThreadSanitizer, Mutex) {
  Mutex m;
  MainThread t0;
  t0.Create(m);

  ScopedThread t1, t2;
  MemLoc l;
  t1.Lock(m);
  t1.Write1(l);
  t1.Unlock(m);
  t2.Lock(m);
  t2.Write1(l);
  t2.Unlock(m);
  t2.Destroy(m);
}

TEST(ThreadSanitizer, StaticMutex) {
  // Emulates statically initialized mutex.
  Mutex m;
  m.StaticInit();
  {
    ScopedThread t1, t2;
    t1.Lock(m);
    t1.Unlock(m);
    t2.Lock(m);
    t2.Unlock(m);
  }
  MainThread().Destroy(m);
}
