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
  ScopedThread t1, t2;
  MemLoc l;
  Mutex m;
  t1.Create(m);
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
  ScopedThread t1, t2;
  Mutex m;
  m.StaticInit();
  t1.Lock(m);
  t1.Unlock(m);
  t2.Lock(m);
  t2.Unlock(m);
  t2.Destroy(m);
}
