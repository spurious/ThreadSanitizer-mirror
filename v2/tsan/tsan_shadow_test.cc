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
#include "tsan_platform.h"
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
  const int kShadowSize = 8;
  u64 aligned_data[4];
  char *data = (char*)aligned_data;
  CHECK_EQ((uptr)data % kShadowSize, 0);
  uptr s0 = MemToShadow((uptr)&data[0]);
  CHECK_EQ(s0 % kShadowSize, 0);
  for (int i = 1; i < kShadowCnt; i++)
    CHECK_EQ(s0, MemToShadow((uptr)&data[i]));
  for (int i = kShadowCnt; i < 2*kShadowCnt; i++)
    CHECK_EQ(s0 + kShadowSize*kShadowCnt, MemToShadow((uptr)&data[i]));
  for (int i = 2*kShadowCnt; i < 3*kShadowCnt; i++)
    CHECK_EQ(s0 + 2*kShadowSize*kShadowCnt, MemToShadow((uptr)&data[i]));
}

}  // namespace __tsan
