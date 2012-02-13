//===-- tsan_bench.cc -------------------------------------------*- C++ -*-===//
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
#include "tsan_defs.h"
#include "gtest/gtest.h"
#include <stdint.h>

int kSize = 128;
int kRepeat = 2*1024*1024;

void noinstr(void *p) {}

template<typename T, void(*__tsan_mop)(void *p)>
static void Benchmark() {
  volatile T data[kSize];
  for (int i = 0; i < kRepeat; i++) {
    for (int j = 0; j < kSize; j++) {
      __tsan_mop((void*)&data[j]);
      data[j]++;
    }
  }
}

#if TSAN_DEBUG == 0

TEST(DISABLED_Bench, Mop1) {
  Benchmark<uint8_t, noinstr>();
}

TEST(DISABLED_Bench, Mop1Read) {
  Benchmark<uint8_t, __tsan_read1>();
}

TEST(DISABLED_Bench, Mop1Write) {
  Benchmark<uint8_t, __tsan_write1>();
}

TEST(DISABLED_Bench, Mop2) {
  Benchmark<uint16_t, noinstr>();
}

TEST(DISABLED_Bench, Mop2Read) {
  Benchmark<uint16_t, __tsan_read2>();
}

TEST(DISABLED_Bench, Mop2Write) {
  Benchmark<uint16_t, __tsan_write2>();
}

TEST(DISABLED_Bench, Mop4) {
  Benchmark<uint32_t, noinstr>();
}

TEST(DISABLED_Bench, Mop4Read) {
  Benchmark<uint32_t, __tsan_read4>();
}

TEST(DISABLED_Bench, Mop4Write) {
  Benchmark<uint32_t, __tsan_write4>();
}

TEST(DISABLED_Bench, Mop8) {
  Benchmark<uint8_t, noinstr>();
}

TEST(DISABLED_Bench, Mop8Read) {
  Benchmark<uint64_t, __tsan_read8>();
}

TEST(DISABLED_Bench, Mop8Write) {
  Benchmark<uint64_t, __tsan_write8>();
}

TEST(DISABLED_Bench, FuncCall) {
  for (int i = 0; i < kRepeat; i++) {
    for (int j = 0; j < kSize; j++)
      __tsan_func_entry((void*)(uintptr_t)j);
    for (int j = 0; j < kSize; j++)
      __tsan_func_exit();
  }
}

#endif  // TSAN_DEBUG == 0
