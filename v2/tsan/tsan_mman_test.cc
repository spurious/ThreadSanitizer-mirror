//===-- tsan_mman_test.cc ---------------------------------------*- C++ -*-===//
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
#include "tsan_mman.h"
#include "tsan_rtl.h"
#include "gtest/gtest.h"

namespace __tsan {

TEST(Mman, Internal) {
  ScopedInRtl in_rtl;
  ThreadState *thr = cur_thread();
  char *p = (char*)internal_alloc(thr, 10);
  EXPECT_NE(p, (char*)0);
  char *p2 = (char*)internal_alloc(thr, 20);
  EXPECT_NE(p2, (char*)0);
  EXPECT_NE(p2, p);
  for (int i = 0; i < 10; i++) {
    p[i] = 42;
  }
  for (int i = 0; i < 20; i++) {
    ((char*)p2)[i] = 42;
  }
  internal_free(thr, p);
  internal_free(thr, p2);
}

TEST(Mman, User) {
  ScopedInRtl in_rtl;
  ThreadState *thr = cur_thread();
  uptr pc = 0;
  char *p = (char*)user_alloc(thr, pc, 10);
  EXPECT_NE(p, (char*)0);
  char *p2 = (char*)user_alloc(thr, pc, 20);
  EXPECT_NE(p2, (char*)0);
  EXPECT_NE(p2, p);
  MBlock *b = user_mblock(thr, p);
  EXPECT_NE(b, (MBlock*)0);
  EXPECT_EQ(b->size, (uptr)10);
  MBlock *b2 = user_mblock(thr, p2);
  EXPECT_NE(b2, (MBlock*)0);
  EXPECT_EQ(b2->size, (uptr)20);
  for (int i = 0; i < 10; i++) {
    p[i] = 42;
    EXPECT_EQ(b, user_mblock(thr, p + i));
  }
  for (int i = 0; i < 20; i++) {
    ((char*)p2)[i] = 42;
    EXPECT_EQ(b2, user_mblock(thr, p2 + i));
  }
  user_free(thr, pc, p);
  user_free(thr, pc, p2);
}

}  // namespace __tsan
