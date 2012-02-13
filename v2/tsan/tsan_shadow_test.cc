//===-- tsan_shadow_test.cc -------------------------------------*- C++ -*-===//
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
#include "tsan_linux.h"
#include "gtest/gtest.h"

namespace __tsan {

TEST(Shadow, Mapping) {
  static int global;
  int stack;
  void *heap = malloc(0);
  free(heap);

  CHECK(IsAppMem((uptr)&global));
  CHECK(IsAppMem((uptr)&stack));
  CHECK(IsAppMem((uptr)heap));

  CHECK(IsShadowMem(MemToShadow((uptr)&global)));
  CHECK(IsShadowMem(MemToShadow((uptr)&stack)));
  CHECK(IsShadowMem(MemToShadow((uptr)heap)));
}

TEST(Shadow, Celling) {
  union {
    char data[32];
    u64 aligner;
  };
  CHECK_EQ((uptr)data % 8, 0);
  uptr s0 = MemToShadow((uptr)&data[0]);
  CHECK_EQ(s0 % 8, 0);
  for (int i = 1; i < 8; i++)
    CHECK_EQ(s0, MemToShadow((uptr)&data[i]));
  for (int i = 8; i < 16; i++)
    CHECK_EQ(s0 + 8*kShadowCnt, MemToShadow((uptr)&data[i]));
  for (int i = 16; i < 32; i++)
    CHECK_EQ(s0 + 2*8*kShadowCnt, MemToShadow((uptr)&data[i]));
}

}  // namespace __tsan
