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

TEST(ThreadSanitizer, ThreadTest) {
  ScopedThread t;
  MemLoc l1, l2;
  t.Read<1>(l1);
}


int main(int argc, char **argv) {
  __tsan_init();
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
  return 0;
}
