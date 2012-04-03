//===-- tsan_rtl_mutex.cc ---------------------------------------*- C++ -*-===//
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

#include "tsan_rtl.h"
#include "tsan_sync.h"

namespace __tsan {

void MutexCreate(ThreadState *thr, uptr pc, uptr addr,
                 bool rw, bool recursive) {
  CHECK_GT(thr->in_rtl, 0);
  DPrintf("#%d: MutexCreate %p\n", thr->fast_state.tid(), addr);
  MemoryWrite1Byte(thr, pc, addr);
  SyncVar *s = CTX()->synctab.GetAndLock(&thr->syncslab, addr, true);
  s->is_rw = rw;
  s->is_recursive = recursive;
  s->mtx.Unlock();
}

void MutexDestroy(ThreadState *thr, uptr pc, uptr addr) {
  CHECK_GT(thr->in_rtl, 0);
  DPrintf("#%d: MutexDestroy %p\n", thr->fast_state.tid(), addr);
  MemoryWrite1Byte(thr, pc, addr);
  SyncVar *s = CTX()->synctab.GetAndRemove(addr);
  if (s == 0)
    return;
  if (s->owner_tid != SyncVar::kInvalidTid)
    Printf("ThreadSanitizer WARNING: destroy of a locked mutex\n");
  s->clock.Free(&thr->clockslab);
  s->read_clock.Free(&thr->clockslab);
  s->~SyncVar();
  thr->syncslab.Free(s);
}

void MutexLock(ThreadState *thr, uptr pc, uptr addr) {
  CHECK_GT(thr->in_rtl, 0);
  DPrintf("#%d: MutexLock %p\n", thr->fast_state.tid(), addr);
  MemoryRead1Byte(thr, pc, addr);
  thr->fast_state.IncrementEpoch();
  TraceAddEvent(thr, thr->fast_state.epoch(), EventTypeLock, addr);
  SyncVar *s = CTX()->synctab.GetAndLock(&thr->syncslab, addr, true);
  if (s->owner_tid == SyncVar::kInvalidTid) {
    CHECK_EQ(s->recursion, 0);
    s->owner_tid = thr->fast_state.tid();
  } else if (s->owner_tid == thr->fast_state.tid()) {
    CHECK_GT(s->recursion, 0);
  } else {
    Printf("ThreadSanitizer WARNING: double lock\n");
  }
  if (s->recursion == 0) {
    thr->clock.set(thr->fast_state.tid(), thr->fast_state.epoch());
    thr->clock.acquire(&s->clock);
    thr->clock.acquire(&s->read_clock);
  } else if (!s->is_recursive) {
    Printf("ThreadSanitizer WARNING: recursive lock\n");
  }
  s->recursion++;
  s->mtx.Unlock();
}

void MutexUnlock(ThreadState *thr, uptr pc, uptr addr) {
  CHECK_GT(thr->in_rtl, 0);
  DPrintf("#%d: MutexUnlock %p\n", thr->fast_state.tid(), addr);
  MemoryRead1Byte(thr, pc, addr);
  thr->fast_state.IncrementEpoch();
  TraceAddEvent(thr, thr->fast_state.epoch(), EventTypeUnlock, addr);
  SyncVar *s = CTX()->synctab.GetAndLock(&thr->syncslab, addr, true);
  if (s->recursion == 0) {
    Printf("ThreadSanitizer WARNING: unlock of unlocked mutex\n");
  } else if (s->owner_tid != thr->fast_state.tid()) {
    Printf("ThreadSanitizer WARNING: mutex unlock by another thread\n");
  } else {
    s->recursion--;
    if (s->recursion == 0) {
      s->owner_tid = SyncVar::kInvalidTid;
      thr->clock.set(thr->fast_state.tid(), thr->fast_state.epoch());
      thr->fast_synch_epoch = thr->fast_state.epoch();
      thr->clock.release(&s->clock, &thr->clockslab);
    }
  }
  s->mtx.Unlock();
}

void MutexReadLock(ThreadState *thr, uptr pc, uptr addr) {
  CHECK_GT(thr->in_rtl, 0);
  DPrintf("#%d: MutexReadLock %p\n", thr->fast_state.tid(), addr);
  MemoryRead1Byte(thr, pc, addr);
  thr->fast_state.IncrementEpoch();
  TraceAddEvent(thr, thr->fast_state.epoch(), EventTypeRLock, addr);
  SyncVar *s = CTX()->synctab.GetAndLock(&thr->syncslab, addr, false);
  if (s->owner_tid != SyncVar::kInvalidTid)
    Printf("ThreadSanitizer WARNING: read lock of a write locked mutex\n");
  thr->clock.set(thr->fast_state.tid(), thr->fast_state.epoch());
  thr->clock.acquire(&s->clock);
  s->mtx.ReadUnlock();
}

void MutexReadUnlock(ThreadState *thr, uptr pc, uptr addr) {
  CHECK_GT(thr->in_rtl, 0);
  DPrintf("#%d: MutexReadUnlock %p\n", thr->fast_state.tid(), addr);
  MemoryRead1Byte(thr, pc, addr);
  thr->fast_state.IncrementEpoch();
  TraceAddEvent(thr, thr->fast_state.epoch(), EventTypeRUnlock, addr);
  SyncVar *s = CTX()->synctab.GetAndLock(&thr->syncslab, addr, true);
  if (s->owner_tid != SyncVar::kInvalidTid)
    Printf("ThreadSanitizer WARNING: read unlock of a write locked mutex\n");
  thr->clock.set(thr->fast_state.tid(), thr->fast_state.epoch());
  thr->fast_synch_epoch = thr->fast_state.epoch();
  thr->clock.release(&s->read_clock, &thr->clockslab);
  s->mtx.Unlock();
}

void MutexReadOrWriteUnlock(ThreadState *thr, uptr pc, uptr addr) {
  CHECK_GT(thr->in_rtl, 0);
  DPrintf("#%d: MutexReadOrWriteUnlock %p\n", thr->fast_state.tid(), addr);
  MemoryRead1Byte(thr, pc, addr);
  SyncVar *s = CTX()->synctab.GetAndLock(&thr->syncslab, addr, true);
  if (s->owner_tid == SyncVar::kInvalidTid) {
    // Seems to be read unlock.
    thr->fast_state.IncrementEpoch();
    TraceAddEvent(thr, thr->fast_state.epoch(), EventTypeRUnlock, addr);
    thr->clock.set(thr->fast_state.tid(), thr->fast_state.epoch());
    thr->fast_synch_epoch = thr->fast_state.epoch();
    thr->clock.release(&s->read_clock, &thr->clockslab);
  } else if (s->owner_tid == thr->fast_state.tid()) {
    // Seems to be write unlock.
    CHECK_GT(s->recursion, 0);
    s->recursion--;
    if (s->recursion == 0) {
      s->owner_tid = SyncVar::kInvalidTid;
      thr->fast_state.IncrementEpoch();
      TraceAddEvent(thr, thr->fast_state.epoch(), EventTypeUnlock, addr);
      thr->clock.set(thr->fast_state.tid(), thr->fast_state.epoch());
      thr->fast_synch_epoch = thr->fast_state.epoch();
      thr->clock.release(&s->clock, &thr->clockslab);
    }
  } else {
    Printf("ThreadSanitizer WARNING: mutex unlock by another thread\n");
  }
  s->mtx.Unlock();
}

void Acquire(ThreadState *thr, uptr pc, uptr addr) {
  CHECK_GT(thr->in_rtl, 0);
  DPrintf("#%d: Acquire %p\n", thr->fast_state.tid(), addr);
  SyncVar *s = CTX()->synctab.GetAndLock(&thr->syncslab, addr, false);
  thr->clock.set(thr->fast_state.tid(), thr->fast_state.epoch());
  thr->clock.acquire(&s->clock);
  s->mtx.ReadUnlock();
}

void Release(ThreadState *thr, uptr pc, uptr addr) {
  CHECK_GT(thr->in_rtl, 0);
  DPrintf("#%d: Release %p\n", thr->fast_state.tid(), addr);
  SyncVar *s = CTX()->synctab.GetAndLock(&thr->syncslab, addr, true);
  thr->clock.set(thr->fast_state.tid(), thr->fast_state.epoch());
  thr->clock.release(&s->clock, &thr->clockslab);
  s->mtx.Unlock();
}

}  // namespace __tsan
