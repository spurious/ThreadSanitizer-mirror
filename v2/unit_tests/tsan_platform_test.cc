//===-- tsan_platform_test.cc -----------------------------------*- C++ -*-===//
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
#include "tsan_platform.h"
#include "gtest/gtest.h"

namespace __tsan {

static void *TestThreadInfo(void *arg) {
  ScopedInRtl in_rtl;
  uptr stk_addr = 0;
  uptr stk_size = 0;
  uptr tls_addr = 0;
  uptr tls_size = 0;
  GetThreadStackAndTls(&stk_addr, &stk_size, &tls_addr, &tls_size);
  // Printf("stk=%lx-%lx(%lu)\n", stk_addr, stk_addr + stk_size, stk_size);
  // Printf("tls=%lx-%lx(%lu)\n", tls_addr, tls_addr + tls_size, tls_size);

  int stack_var;
  EXPECT_NE(stk_addr, (uptr)0);
  EXPECT_NE(stk_size, (uptr)0);
  EXPECT_GT((uptr)&stack_var, stk_addr);
  EXPECT_LT((uptr)&stack_var, stk_addr + stk_size);

  static __thread int thread_var;
  EXPECT_NE(tls_addr, (uptr)0);
  EXPECT_NE(tls_size, (uptr)0);
  EXPECT_GT((uptr)&thread_var, tls_addr);
  EXPECT_LT((uptr)&thread_var, tls_addr + tls_size);

  // Ensure that tls and stack do not intersect.
  uptr tls_end = tls_addr + tls_size;
  EXPECT_TRUE(tls_addr < stk_addr || tls_addr >= stk_addr + stk_size);
  EXPECT_TRUE(tls_end  < stk_addr || tls_end  >=  stk_addr + stk_size);
  EXPECT_TRUE((tls_addr < stk_addr) == (tls_end  < stk_addr));
  return 0;
}

TEST(Platform, ThreadInfoMain) {
  TestThreadInfo(0);
}

TEST(Platform, ThreadInfoWorker) {
  pthread_t t;
  pthread_create(&t, 0, TestThreadInfo, 0);
  pthread_join(t, 0);
}

}  // namespace __tsan
