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
#include <stdlib.h>

namespace __tsan {

TEST(Slab, Basic) {
  // Allocate a batch from SlabAlloc, free the batch.
  SlabAlloc alloc(16);
  uptr n = 0;
  void *p = alloc.Alloc(&n);
  CHECK_NE(p, NULL);
  CHECK_NE(n, 0);
  void *last = p;
  for (uptr i = 0; i < n-1; i++)
    last = *(void**)last;
  CHECK_NE(last, NULL);
  CHECK_EQ(*(void**)last, NULL);
  alloc.Free(p, last, n);
}

TEST(Slab, Cache) {
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

}  // namespace __tsan
