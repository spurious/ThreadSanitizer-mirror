//===-- tsan_clock_test.cc --------------------------------------*- C++ -*-===//
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
#include "tsan_clock.h"
#include "gtest/gtest.h"

namespace __tsan {

TEST(Clock, VectorBasic) {
  VectorClock clk;
  clk.Init();
  CHECK_EQ(clk.size(), 0);
  clk.tick(0);
  CHECK_EQ(clk.size(), 1);
  CHECK_EQ(clk.get(0), 1);
  clk.tick(3);
  CHECK_EQ(clk.size(), 4);
  CHECK_EQ(clk.get(0), 1);
  CHECK_EQ(clk.get(1), 0);
  CHECK_EQ(clk.get(2), 0);
  CHECK_EQ(clk.get(3), 1);
  clk.tick(3);
  CHECK_EQ(clk.get(3), 2);
}

TEST(Clock, ChunkedBasic) {
  SlabAlloc alloc(ChunkedClock::kChunkSize);
  SlabCache slab(&alloc);
  VectorClock vector;
  vector.Init();
  ChunkedClock chunked;
  CHECK_EQ(vector.size(), 0);
  CHECK_EQ(chunked.size(), 0);
  vector.acquire(&chunked);
  CHECK_EQ(vector.size(), 0);
  CHECK_EQ(chunked.size(), 0);
  vector.release(&chunked, &slab);
  CHECK_EQ(vector.size(), 0);
  CHECK_EQ(chunked.size(), 0);
  vector.acq_rel(&chunked, &slab);
  CHECK_EQ(vector.size(), 0);
  CHECK_EQ(chunked.size(), 0);
  chunked.Free(&slab);
}

TEST(Clock, AcquireRelease) {
  SlabAlloc alloc(ChunkedClock::kChunkSize);
  SlabCache slab(&alloc);
  VectorClock vector1;
  vector1.Init();
  vector1.tick(100);
  ChunkedClock chunked;
  vector1.release(&chunked, &slab);
  CHECK_EQ(chunked.size(), 101);
  VectorClock vector2;
  vector2.Init();
  vector2.acquire(&chunked);
  CHECK_EQ(vector2.size(), 101);
  CHECK_EQ(vector2.get(0), 0);
  CHECK_EQ(vector2.get(1), 0);
  CHECK_EQ(vector2.get(99), 0);
  CHECK_EQ(vector2.get(100), 1);
  chunked.Free(&slab);
}

TEST(Clock, ManyThreads) {
  SlabAlloc alloc(ChunkedClock::kChunkSize);
  SlabCache slab(&alloc);
  ChunkedClock chunked;
  for (int i = 0; i < 100; i++) {
    VectorClock vector;
    vector.Init();
    vector.tick(i);
    vector.release(&chunked, &slab);
    CHECK_EQ(chunked.size(), i + 1);
    vector.acquire(&chunked);
    CHECK_EQ(vector.size(), i + 1);
  }
  VectorClock vector;
  vector.Init();
  vector.acquire(&chunked);
  CHECK_EQ(vector.size(), 100);
  for (int i = 0; i < 100; i++)
    CHECK_EQ(vector.get(i), 1);
  chunked.Free(&slab);
}

TEST(Clock, DifferentSizes) {
  SlabAlloc alloc(ChunkedClock::kChunkSize);
  SlabCache slab(&alloc);
  {
    VectorClock vector1;
    vector1.Init();
    vector1.tick(10);
    VectorClock vector2;
    vector2.Init();
    vector2.tick(20);
    {
      ChunkedClock chunked;
      vector1.release(&chunked, &slab);
      CHECK_EQ(chunked.size(), 11);
      vector2.release(&chunked, &slab);
      CHECK_EQ(chunked.size(), 21);
      chunked.Free(&slab);
    }
    {
      ChunkedClock chunked;
      vector2.release(&chunked, &slab);
      CHECK_EQ(chunked.size(), 21);
      vector1.release(&chunked, &slab);
      CHECK_EQ(chunked.size(), 21);
      chunked.Free(&slab);
    }
    {
      ChunkedClock chunked;
      vector1.release(&chunked, &slab);
      vector2.acquire(&chunked);
      CHECK_EQ(vector2.size(), 21);
      chunked.Free(&slab);
    }
    {
      ChunkedClock chunked;
      vector2.release(&chunked, &slab);
      vector1.acquire(&chunked);
      CHECK_EQ(vector1.size(), 21);
      chunked.Free(&slab);
    }
  }
}

}  // namespace __tsan
