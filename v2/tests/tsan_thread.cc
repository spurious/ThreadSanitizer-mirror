//===-- tsan_thread.cc ------------------------------------------*- C++ -*-===//
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
#include "tsan_test_util.h"
#include "gtest/gtest.h"

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

TEST(DISABLED_ThreadSanitizer, ThreadALot) {
  const int kThreads = 50000;
  for (int i = 0; i < kThreads; i++) {
    ScopedThread thread;
    (void)thread;
  }
}

TEST(DISABLED_ThreadSanitizer, ThreadALot2) {
  const int kThreads = 50000;
  const int kAlive = 1000;
  ScopedThread *threads[kAlive] = {};
  for (int i = 0; i < kThreads; i++) {
    if (threads[i % kAlive])
      delete threads[i % kAlive];
    threads[i % kAlive] = new ScopedThread;
  }
  for (int i = 0; i < kAlive; i++) {
    delete threads[i];
  }
}
