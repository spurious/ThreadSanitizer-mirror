//===-- tsan_sync_test.cc ---------------------------------------*- C++ -*-===//
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
#include "tsan_sync.h"
#include "tsan_slab.h"
#include "gtest/gtest.h"

#include <stdlib.h>
#include <map>

namespace __tsan {

TEST(Sync, Table) {
  const int kIters = 512*1024;
  const int kRange = 10000;

  SlabAlloc alloc(sizeof(SyncVar));
  SlabCache slab(&alloc);
  SyncTab tab;
  SyncVar *golden[kRange] = {};
  for (int i = 0; i < kIters; i++) {
    int addr = rand() % (kRange - 1) + 1;
    if (rand() % 2) {
      // Get or add.
      SyncVar *v = tab.GetAndLock(&slab, addr, true);
      CHECK(golden[addr] == 0 || golden[addr] == v);
      CHECK(v->addr == addr);
      golden[addr] = v;
      v->mtx.Unlock();
    } else {
      // Remove.
      SyncVar *v = tab.GetAndRemove(addr);
      CHECK(golden[addr] == v);
      if (v) {
        CHECK(v->addr == addr);
        golden[addr] = 0;
        v->~SyncVar();
        slab.Free(v);
      }
    }
  }
  for (int addr = 0; addr < kRange; addr++) {
    if (golden[addr] == 0)
      continue;
    SyncVar *v = tab.GetAndRemove(addr);
    CHECK(v == golden[addr]);
    CHECK(v->addr == addr);
    v->~SyncVar();
    slab.Free(v);
  }
}

}  // namespace __tsan
