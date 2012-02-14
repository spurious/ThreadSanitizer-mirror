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

TEST(ThreadSanitizer, ThreadSync) {
  MainThread t0;
  MemLoc l;
  t0.Write1(l);
  {
    ScopedThread t1;
    t1.Write1(l);
  }
  t0.Write1(l);
}

TEST(ThreadSanitizer, ThreadDetach1) {
  ScopedThread t1(true);
  MemLoc l;
  t1.Write1(l);
}

TEST(ThreadSanitizer, ThreadDetach2) {
  ScopedThread t1;
  MemLoc l;
  t1.Write1(l);
  t1.Detach();
}

int main(int argc, char **argv) {
  TestMutexBeforeInit();  // Mutexes must be usable before __tsan_init();
  __tsan_init();
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
