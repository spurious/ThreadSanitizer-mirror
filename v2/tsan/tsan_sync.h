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
  enum Type { Atomic, Mtx, Sem };

  SyncVar(Type type, uptr addr);

  // The following functions emulate read/write on the mutex address.
  // That is used to detect races between e.g. mutex create/lock.
  void Read(ThreadState *thr, uptr pc);
  void Write(ThreadState *thr, uptr pc);

  const Type type;
  const uptr addr;
  Mutex mtx;
  ChunkedClock clock;
  SyncVar *next;  // In SyncTab hashtable.
};

struct MutexVar : SyncVar {
  MutexVar(uptr addr, bool is_rw);
  const bool is_rw;
};

class SyncTab {
 public:
  SyncTab();

  void insert(SyncVar *var);
  SyncVar* GetAndLockIfExists(uptr addr);
  SyncVar* GetAndRemoveIfExists(uptr addr);

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
