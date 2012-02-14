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

#include "tsan_defs.h"
#include "tsan_clock.h"
#include "tsan_mutex.h"
// #include "tsan_rtl.h"

namespace __tsan {

struct ThreadState;

class PoorMansMap {
 public:
  PoorMansMap();
  ~PoorMansMap();
  bool Insert(uptr key, uptr value);
  bool Erase(uptr key);
  bool Get(uptr key, uptr *value);
 private:
  struct Impl;
  Impl *impl_;
};

// Both K and V are pointer-sized PODs.
template <class K, class V>
class Map : private PoorMansMap {
 public:
  bool Insert(K key, V value) {
    return PoorMansMap::Insert((uptr)(key), (uptr)(value));
  }
  bool Erase(K key) {
    return PoorMansMap::Erase((uptr)(key));
  }
  bool Get(K key, V *value) {
    return PoorMansMap::Get((uptr)(key), (uptr*)(value));
  }
};

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
  Mutex mtx_;
  typedef Map<uptr, SyncVar*> tab_t;
  tab_t tab_;

  SyncTab(const SyncTab&);  // Not implemented.
  void operator = (const SyncTab&);  // Not implemented.
};

}  // namespace __tsan

#endif  // TSAN_SYNC_H
