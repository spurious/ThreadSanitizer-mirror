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
#include "gtest/gtest.h"
#include <stdint.h>

volatile int kSize = 1024;
volatile int kRepeat = 256*1024;

TEST(Bench, Write8NonInstrumented) {
  volatile uint64_t data[kSize];
  for (int i = 0; i < kRepeat; i++) {
    for (int j = 0; j < kSize; j++) {
      data[j]++;
    }
  }
}

TEST(Bench, Write8Instrumented) {
  if (TSAN_DEBUG) {
    printf("Debug build will produce enormous debug output. Skipping.\n");
    return;
  }
  volatile uint64_t data[kSize];
  for (int i = 0; i < kRepeat; i++) {
    for (int j = 0; j < kSize; j++) {
      __tsan_write8((void*)&data[j]);
      data[j]++;
    }
  }
}
