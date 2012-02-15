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

void MutexCreate(ThreadState *thr, uptr pc, uptr addr, bool is_rw) {
  DPrintf("#%d: MutexCreate %p\n", thr->fast.tid, addr);
  MemoryAccess(thr, pc, addr, 1, true);
  SyncVar *s = ctx->synctab.GetAndLock(&thr->syncslab, addr, true);
  s->mtx.Unlock();
}

void MutexDestroy(ThreadState *thr, uptr pc, uptr addr) {
  DPrintf("#%d: MutexDestroy %p\n", thr->fast.tid, addr);
  MemoryAccess(thr, pc, addr, 1, true);
  SyncVar *s = ctx->synctab.GetAndRemove(addr);
  if (s == 0)
    return;
  s->clock.Free(&thr->clockslab);
  s->~SyncVar();
  thr->syncslab.Free(s);
}

void MutexLock(ThreadState *thr, uptr pc, uptr addr) {
  DPrintf("#%d: MutexLock %p\n", thr->fast.tid, addr);
  MemoryAccess(thr, pc, addr, 1, false);
  thr->fast.epoch++;
  TraceAddEvent(thr, thr->fast.epoch, EventTypeLock, addr);
  SyncVar *s = ctx->synctab.GetAndLock(&thr->syncslab, addr, false);
  thr->clock.set(thr->fast.tid, thr->fast.epoch);
  thr->clock.acquire(&s->clock);
  s->mtx.ReadUnlock();
}

void MutexUnlock(ThreadState *thr, uptr pc, uptr addr) {
  DPrintf("#%d: MutexUnlock %p\n", thr->fast.tid, addr);
  MemoryAccess(thr, pc, addr, 1, false);
  thr->fast.epoch++;
  TraceAddEvent(thr, thr->fast.epoch, EventTypeUnlock, addr);
  SyncVar *s = ctx->synctab.GetAndLock(&thr->syncslab, addr, true);
  thr->clock.set(thr->fast.tid, thr->fast.epoch);
  thr->fast_synch_epoch = thr->fast.epoch;
  thr->clock.release(&s->clock, &thr->clockslab);
  s->mtx.Unlock();
}

void Acquire(ThreadState *thr, uptr pc, uptr addr) {
  DPrintf("#%d: Acquire %p\n", thr->fast.tid, addr);
  SyncVar *s = ctx->synctab.GetAndLock(&thr->syncslab, addr, false);
  thr->clock.set(thr->fast.tid, thr->fast.epoch);
  thr->clock.acquire(&s->clock);
  s->mtx.ReadUnlock();
}

void Release(ThreadState *thr, uptr pc, uptr addr) {
  DPrintf("#%d: Release %p\n", thr->fast.tid, addr);
  SyncVar *s = ctx->synctab.GetAndLock(&thr->syncslab, addr, true);
  thr->clock.set(thr->fast.tid, thr->fast.epoch);
  thr->clock.release(&s->clock, &thr->clockslab);
  s->mtx.Unlock();
}

}  // namespace __tsan
