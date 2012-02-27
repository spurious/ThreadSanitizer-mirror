//===-- tsan_slab_test.cc ---------------------------------------*- C++ -*-===//
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
#include "tsan_slab.h"
#include "gtest/gtest.h"

namespace __tsan {

TEST(Alloc, SlabBasic) {
  // Allocate a batch from SlabAlloc, free the batch.
  SlabAlloc alloc(16);
  uptr n = 0;
  void *p = alloc.Alloc(&n);
  EXPECT_NE(p, (void*)0);
  EXPECT_NE(n, (uptr)0);
  void *last = p;
  for (uptr i = 0; i < n-1; i++)
    last = *(void**)last;
  EXPECT_NE(last, (void*)0);
  EXPECT_EQ(*(void**)last, (void*)0);
  alloc.Free(p, last, n);
}

TEST(Alloc, SlabCache) {
  const int kCount = 1000;
  SlabAlloc alloc(16);
  SlabCache cache(&alloc);

  // Allocate one, free one.
  for (int i = 0; i < kCount; i++) {
    void *p = cache.Alloc();
    cache.Free(p);
  }
  // Allocate a batch, free FIFO.
  void *ptrs[kCount];
  for (int i = 0; i < kCount; i++)
    ptrs[i] = cache.Alloc();
  for (int i = 0; i < kCount; i++)
    cache.Free(ptrs[i]);

  // Allocate a batch, free LIFO.
  for (int i = 0; i < kCount; i++)
    ptrs[i] = cache.Alloc();
  for (int i = 0; i < kCount; i++)
    cache.Free(ptrs[kCount - i - 1]);
}

TEST(Alloc, Region) {
  char buf[10];
  RegionAlloc alloc(&buf, sizeof(buf));
  EXPECT_EQ(alloc.Alloc(100), (void*)0);
  EXPECT_EQ(alloc.Alloc(1), &buf[0]);
  EXPECT_EQ(alloc.Alloc(1), &buf[1]);
  EXPECT_EQ(alloc.Alloc(2), &buf[2]);
  EXPECT_EQ(alloc.Alloc(2), &buf[4]);
  EXPECT_EQ(alloc.Alloc(4), &buf[6]);
  EXPECT_EQ(alloc.Alloc(1), (void*)0);
}

}  // namespace __tsan
