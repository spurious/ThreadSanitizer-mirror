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
#include "tsan_report.h"
#include "tsan_symbolize.h"

namespace __tsan {

void MutexCreate(ThreadState *thr, uptr pc, uptr addr,
                 bool rw, bool recursive) {
  CHECK_GT(thr->in_rtl, 0);
  DPrintf("#%d: MutexCreate %lx\n", thr->tid, addr);
  StatInc(thr, StatMutexCreate);
  MemoryWrite1Byte(thr, pc, addr);
  SyncVar *s = CTX()->synctab.GetAndLock(thr, pc, &thr->syncslab, addr, true);
  s->is_rw = rw;
  s->is_recursive = recursive;
  s->mtx.Unlock();
}

void MutexDestroy(ThreadState *thr, uptr pc, uptr addr) {
  CHECK_GT(thr->in_rtl, 0);
  DPrintf("#%d: MutexDestroy %lx\n", thr->tid, addr);
  StatInc(thr, StatMutexDestroy);
  MemoryWrite1Byte(thr, pc, addr);
  SyncVar *s = CTX()->synctab.GetAndRemove(addr);
  if (s == 0)
    return;
  if (s->owner_tid != SyncVar::kInvalidTid && !s->is_broken) {
    s->is_broken = true;

/*
    ScopedReport rep(ReportTypeMutexDestroyLocked);
    rep.AddMutex(s);
    rep.AddLocation(s->addr);
    PrintReport(&rep);
*/


    Lock l(&CTX()->report_mtx);
    ReportDesc &rep = *GetGlobalReport();
    internal_memset(&rep, 0, sizeof(rep));
    RegionAlloc alloc(rep.alloc, sizeof(rep.alloc));
    rep.typ = ReportTypeMutexDestroyLocked;
    rep.nmutex = 1;
    rep.mutex = alloc.Alloc<ReportMutex>(1);
    rep.mutex->id = 42;
    rep.mutex->stack = SymbolizeStack(&alloc, s->creation_stack);
    Symbol symb;
    if (SymbolizeData(&alloc, s->addr, &symb)) {
      rep.loc = alloc.Alloc<ReportLocation>(1);
      rep.loc->type = ReportLocationGlobal;
      rep.loc->addr = s->addr;
      rep.loc->size = 1;
      rep.loc->tid = 0;
      rep.loc->name = symb.name;
      rep.loc->file = symb.file;
      rep.loc->line = symb.line;
      rep.loc->stack = 0;
    }
    PrintReport(&rep);
    CTX()->nreported++;
  }
  s->clock.Free(&thr->clockslab);
  s->read_clock.Free(&thr->clockslab);
  s->creation_stack.Free(thr);
  s->~SyncVar();
  thr->syncslab.Free(s);
}

void MutexLock(ThreadState *thr, uptr pc, uptr addr) {
  CHECK_GT(thr->in_rtl, 0);
  DPrintf("#%d: MutexLock %lx\n", thr->tid, addr);
  MemoryRead1Byte(thr, pc, addr);
  thr->fast_state.IncrementEpoch();
  TraceAddEvent(thr, thr->fast_state.epoch(), EventTypeLock, addr);
  SyncVar *s = CTX()->synctab.GetAndLock(thr, pc, &thr->syncslab, addr, true);
  if (s->owner_tid == SyncVar::kInvalidTid) {
    CHECK_EQ(s->recursion, 0);
    s->owner_tid = thr->tid;
  } else if (s->owner_tid == thr->tid) {
    CHECK_GT(s->recursion, 0);
  } else {
    Printf("ThreadSanitizer WARNING: double lock\n");
  }
  if (s->recursion == 0) {
    StatInc(thr, StatMutexLock);
    thr->clock.set(thr->tid, thr->fast_state.epoch());
    thr->clock.acquire(&s->clock);
    thr->clock.acquire(&s->read_clock);
  } else if (!s->is_recursive) {
    StatInc(thr, StatMutexRecLock);
  }
  s->recursion++;
  s->mtx.Unlock();
}

void MutexUnlock(ThreadState *thr, uptr pc, uptr addr) {
  CHECK_GT(thr->in_rtl, 0);
  DPrintf("#%d: MutexUnlock %lx\n", thr->tid, addr);
  MemoryRead1Byte(thr, pc, addr);
  thr->fast_state.IncrementEpoch();
  TraceAddEvent(thr, thr->fast_state.epoch(), EventTypeUnlock, addr);
  SyncVar *s = CTX()->synctab.GetAndLock(thr, pc, &thr->syncslab, addr, true);
  if (s->recursion == 0) {
    if (!s->is_broken) {
      s->is_broken = true;
      Printf("ThreadSanitizer WARNING: unlock of unlocked mutex\n");
    }
  } else if (s->owner_tid != thr->tid) {
    if (!s->is_broken) {
      s->is_broken = true;
      Printf("ThreadSanitizer WARNING: mutex unlock by another thread\n");
    }
  } else {
    s->recursion--;
    if (s->recursion == 0) {
      StatInc(thr, StatMutexUnlock);
      s->owner_tid = SyncVar::kInvalidTid;
      thr->clock.set(thr->tid, thr->fast_state.epoch());
      thr->fast_synch_epoch = thr->fast_state.epoch();
      thr->clock.release(&s->clock, &thr->clockslab);
    } else {
      StatInc(thr, StatMutexRecUnlock);
    }
  }
  s->mtx.Unlock();
}

void MutexReadLock(ThreadState *thr, uptr pc, uptr addr) {
  CHECK_GT(thr->in_rtl, 0);
  DPrintf("#%d: MutexReadLock %lx\n", thr->tid, addr);
  StatInc(thr, StatMutexReadLock);
  MemoryRead1Byte(thr, pc, addr);
  thr->fast_state.IncrementEpoch();
  TraceAddEvent(thr, thr->fast_state.epoch(), EventTypeRLock, addr);
  SyncVar *s = CTX()->synctab.GetAndLock(thr, pc, &thr->syncslab, addr, false);
  if (s->owner_tid != SyncVar::kInvalidTid)
    Printf("ThreadSanitizer WARNING: read lock of a write locked mutex\n");
  thr->clock.set(thr->tid, thr->fast_state.epoch());
  thr->clock.acquire(&s->clock);
  s->mtx.ReadUnlock();
}

void MutexReadUnlock(ThreadState *thr, uptr pc, uptr addr) {
  CHECK_GT(thr->in_rtl, 0);
  DPrintf("#%d: MutexReadUnlock %lx\n", thr->tid, addr);
  StatInc(thr, StatMutexReadUnlock);
  MemoryRead1Byte(thr, pc, addr);
  thr->fast_state.IncrementEpoch();
  TraceAddEvent(thr, thr->fast_state.epoch(), EventTypeRUnlock, addr);
  SyncVar *s = CTX()->synctab.GetAndLock(thr, pc, &thr->syncslab, addr, true);
  if (s->owner_tid != SyncVar::kInvalidTid)
    Printf("ThreadSanitizer WARNING: read unlock of a write locked mutex\n");
  thr->clock.set(thr->tid, thr->fast_state.epoch());
  thr->fast_synch_epoch = thr->fast_state.epoch();
  thr->clock.release(&s->read_clock, &thr->clockslab);
  s->mtx.Unlock();
}

void MutexReadOrWriteUnlock(ThreadState *thr, uptr pc, uptr addr) {
  CHECK_GT(thr->in_rtl, 0);
  DPrintf("#%d: MutexReadOrWriteUnlock %lx\n", thr->tid, addr);
  MemoryRead1Byte(thr, pc, addr);
  SyncVar *s = CTX()->synctab.GetAndLock(thr, pc, &thr->syncslab, addr, true);
  if (s->owner_tid == SyncVar::kInvalidTid) {
    // Seems to be read unlock.
    StatInc(thr, StatMutexReadUnlock);
    thr->fast_state.IncrementEpoch();
    TraceAddEvent(thr, thr->fast_state.epoch(), EventTypeRUnlock, addr);
    thr->clock.set(thr->tid, thr->fast_state.epoch());
    thr->fast_synch_epoch = thr->fast_state.epoch();
    thr->clock.release(&s->read_clock, &thr->clockslab);
  } else if (s->owner_tid == thr->tid) {
    // Seems to be write unlock.
    CHECK_GT(s->recursion, 0);
    s->recursion--;
    if (s->recursion == 0) {
      StatInc(thr, StatMutexUnlock);
      s->owner_tid = SyncVar::kInvalidTid;
      // FIXME: Refactor me, plz.
      // The sequence of events is quite tricky and doubled in several places.
      // First, it's a bug to increment the epoch w/o writing to the trace.
      // Then, the acquire/release logic can be factored out as well.
      thr->fast_state.IncrementEpoch();
      TraceAddEvent(thr, thr->fast_state.epoch(), EventTypeUnlock, addr);
      thr->clock.set(thr->tid, thr->fast_state.epoch());
      thr->fast_synch_epoch = thr->fast_state.epoch();
      thr->clock.release(&s->clock, &thr->clockslab);
    } else {
      StatInc(thr, StatMutexRecUnlock);
    }
  } else if (!s->is_broken) {
    s->is_broken = true;
    Printf("ThreadSanitizer WARNING: mutex unlock by another thread\n");
  }
  s->mtx.Unlock();
}

void Acquire(ThreadState *thr, uptr pc, uptr addr) {
  CHECK_GT(thr->in_rtl, 0);
  DPrintf("#%d: Acquire %lx\n", thr->tid, addr);
  SyncVar *s = CTX()->synctab.GetAndLock(thr, pc, &thr->syncslab, addr, false);
  thr->clock.set(thr->tid, thr->fast_state.epoch());
  thr->clock.acquire(&s->clock);
  s->mtx.ReadUnlock();
}

void Release(ThreadState *thr, uptr pc, uptr addr) {
  CHECK_GT(thr->in_rtl, 0);
  DPrintf("#%d: Release %lx\n", thr->tid, addr);
  SyncVar *s = CTX()->synctab.GetAndLock(thr, pc, &thr->syncslab, addr, true);
  thr->clock.set(thr->tid, thr->fast_state.epoch());
  thr->clock.release(&s->clock, &thr->clockslab);
  s->mtx.Unlock();
}

}  // namespace __tsan
