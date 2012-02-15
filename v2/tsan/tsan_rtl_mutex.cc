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
  SyncVar *s = new MutexVar(addr, is_rw);
  ctx->synctab->insert(s);
  s->Write(thr, pc);
}

void MutexDestroy(ThreadState *thr, uptr pc, uptr addr) {
  DPrintf("#%d: MutexDestroy %p\n", thr->fast.tid, addr);
  SyncVar *s = ctx->synctab->GetAndRemoveIfExists(addr);
  CHECK(s && s->type == SyncVar::Mtx);
  s->Write(thr, pc);
  s->clock.Free(thr->clockslab);
  delete s;
}

void MutexLock(ThreadState *thr, uptr pc, uptr addr) {
  DPrintf("#%d: MutexLock %p\n", thr->fast.tid, addr);
  thr->fast.epoch++;
  TraceAddEvent(thr, thr->fast.epoch, EventTypeLock, addr);
  SyncVar *s = ctx->synctab->GetAndLockIfExists(addr);
  if (!s) {
    // Locking a mutex before if was created (e.g. for linked-inited mutexes.
    // FIXME: is that right?
    s = new MutexVar(addr, true);
    s->mtx.Lock();
    ctx->synctab->insert(s);
  }
  s->Read(thr, pc);
  CHECK(s && s->type == SyncVar::Mtx);
  MutexVar *m = static_cast<MutexVar*>(s);
  thr->clock.set(thr->fast.tid, thr->fast.epoch);
  thr->clock.acquire(&m->clock);
  m->mtx.Unlock();
}

void MutexUnlock(ThreadState *thr, uptr pc, uptr addr) {
  DPrintf("#%d: MutexUnlock %p\n", thr->fast.tid, addr);
  thr->fast.epoch++;
  TraceAddEvent(thr, thr->fast.epoch, EventTypeUnlock, addr);
  SyncVar *s = ctx->synctab->GetAndLockIfExists(addr);
  CHECK(s && s->type == SyncVar::Mtx);
  s->Read(thr, pc);
  MutexVar *m = static_cast<MutexVar*>(s);
  thr->clock.set(thr->fast.tid, thr->fast.epoch);
  thr->fast_synch_epoch = thr->fast.epoch;
  thr->clock.release(&m->clock, thr->clockslab);
  m->mtx.Unlock();
}

void Acquire(ThreadState *thr, uptr pc, uptr addr) {
  DPrintf("#%d: Acquire %p\n", thr->fast.tid, addr);
  SyncVar *s = ctx->synctab->GetAndLockIfExists(addr);
  if (!s) {
    s = new SyncVar(SyncVar::Atomic, addr);
    s->mtx.Lock();
    ctx->synctab->insert(s);
  }
  thr->clock.set(thr->fast.tid, thr->fast.epoch);
  thr->clock.acquire(&s->clock);
  s->mtx.Unlock();
}

void Release(ThreadState *thr, uptr pc, uptr addr) {
  DPrintf("#%d: Release %p\n", thr->fast.tid, addr);
  SyncVar *s = ctx->synctab->GetAndLockIfExists(addr);
  if (!s) {
    s = new SyncVar(SyncVar::Atomic, addr);
    ctx->synctab->insert(s);
  }
  thr->clock.set(thr->fast.tid, thr->fast.epoch);
  thr->clock.release(&s->clock, thr->clockslab);
  s->mtx.Unlock();
}

}  // namespace __tsan
