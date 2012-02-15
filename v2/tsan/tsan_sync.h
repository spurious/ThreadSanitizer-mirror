//===-- tsan_sync.h ---------------------------------------------*- C++ -*-===//
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
#ifndef TSAN_SYNC_H
#define TSAN_SYNC_H

#include "tsan_atomic.h"
#include "tsan_clock.h"
#include "tsan_defs.h"
#include "tsan_mutex.h"

namespace __tsan {

struct ThreadState;

struct SyncVar {
  explicit SyncVar(uptr addr);

  Mutex mtx;
  const uptr addr;
  ChunkedClock clock;
  SyncVar *next;  // In SyncTab hashtable.
};

class SyncTab {
 public:
  SyncTab();

  // If the SyncVar does not exist yet, it is created.
  SyncVar* GetAndLock(uptr addr, bool write_lock);

  // If the SyncVar does not exist, returns 0.
  SyncVar* GetAndRemove(uptr addr);

 private:
  struct Part {
    Mutex mtx;
    SyncVar *val;
    char pad[kCacheLineSize - sizeof(Mutex) - sizeof(SyncVar*)];  // NOLINT
    Part();
  };

  static const int kPartCount = 1009;
  Part tab_[kPartCount];

  int PartIdx(uptr addr);

  SyncTab(const SyncTab&);  // Not implemented.
  void operator = (const SyncTab&);  // Not implemented.
};

}  // namespace __tsan

#endif  // TSAN_SYNC_H
