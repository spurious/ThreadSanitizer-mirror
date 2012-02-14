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

// #include "tsan_linux.h"
#include "tsan_rtl.h"
// #include "tsan_interface.h"
// #include "tsan_atomic.h"
// #include "tsan_suppressions.h"
// #include "tsan_symbolize.h"
#include "tsan_sync.h"
// #include "tsan_report.h"

namespace __tsan {

void MutexCreate(ThreadState *thr, uptr pc, uptr addr, bool is_rw) {
  if (TSAN_DEBUG)
    Printf("#%d: MutexCreate %p\n", thr->fast.tid, addr);
  SyncVar *s = new MutexVar(addr, is_rw);
  ctx->synctab->insert(s);
  s->Write(thr, pc);
}

void MutexDestroy(ThreadState *thr, uptr pc, uptr addr) {
  if (TSAN_DEBUG)
    Printf("#%d: MutexDestroy %p\n", thr->fast.tid, addr);
  SyncVar *s = ctx->synctab->GetAndRemoveIfExists(addr);
  CHECK(s && s->type == SyncVar::Mtx);
  s->Write(thr, pc);
  s->clock.Free(thr->clockslab);
  delete s;
}

void MutexLock(ThreadState *thr, uptr pc, uptr addr) {
  if (TSAN_DEBUG)
    Printf("#%d: MutexLock %p\n", thr->fast.tid, addr);
  thr->fast.epoch++;
  TraceAddEvent(thr, thr->fast.epoch, EventTypeLock, addr);
  SyncVar *s = ctx->synctab->GetAndLockIfExists(addr);
  if (!s) {
    // Locking a mutex before if was created (e.g. for linked-inited mutexes.
    // FIXME: is that right?
    s = new MutexVar(addr, true);
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
  if (TSAN_DEBUG)
    Printf("#%d: MutexUnlock %p\n", thr->fast.tid, addr);
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

}  // namespace __tsan
