//===-- tsan_test.cc --------------------------------------------*- C++ -*-===//
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

static void foo() {}
static void bar() {}

TEST(ThreadSanitizer, SimpleWrite) {
  ScopedThread t;
  MemLoc l;
  t.Write1(l);
}

TEST(ThreadSanitizer, SimpleWriteWrite) {
  ScopedThread t1, t2;
  MemLoc l1, l2;
  t1.Write1(l1);
  t2.Write1(l2);
}

TEST(ThreadSanitizer, WriteWriteRace) {
  ScopedThread t1, t2;
  MemLoc l;
  t1.Write1(l);
  t2.Write1(l, true);
}

TEST(ThreadSanitizer, ReadWriteRace) {
  ScopedThread t1, t2;
  MemLoc l;
  t1.Read1(l);
  t2.Write1(l, true);
}

TEST(ThreadSanitizer, WriteReadRace) {
  ScopedThread t1, t2;
  MemLoc l;
  t1.Write1(l);
  t2.Read1(l, true);
}

TEST(ThreadSanitizer, ReadReadNoRace) {
  ScopedThread t1, t2;
  MemLoc l;
  t1.Read1(l);
  t2.Read1(l);
}

TEST(ThreadSanitizer, FuncCall) {
  ScopedThread t1, t2;
  MemLoc l;
  t1.Write1(l);
  t2.Call(foo);
  t2.Call(bar);
  t2.Write1(l, true);
  t2.Return();
  t2.Return();
}

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

int main(int argc, char **argv) {
  __tsan_init();
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
  return 0;
}
