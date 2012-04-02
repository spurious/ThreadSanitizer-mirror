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

TEST(Platform, VirtualAlloc) {
  ScopedInRtl in_rtl;
  void *p1 = virtual_alloc(1);
  void *p2 = virtual_alloc(1);
  EXPECT_NE(p1, (void*)0);
  EXPECT_NE(p2, (void*)0);
  EXPECT_NE(p1, p2);
  virtual_free(p1, 1);
  virtual_free(p2, 1);

  for (int i = 0; i < 1000; i++) {
    void *p = virtual_alloc(16*1024);
    EXPECT_NE(p, (void*)0);
    virtual_free(p, 16*1024);
  }
}

TEST(Platform, ThreadInfo) {
  ScopedInRtl in_rtl;
  uptr stk_addr = 0;
  uptr stk_size = 0;
  uptr tls_addr = 0;
  uptr tls_size = 0;
  GetThreadStackAndTls(&stk_addr, &stk_size, &tls_addr, &tls_size);

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
}

}  // namespace __tsan
