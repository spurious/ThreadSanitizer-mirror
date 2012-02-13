//===-- tsan_rtl.cc ---------------------------------------------*- C++ -*-===//
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
// Main file (entry points) for the TSan run-time.
//===----------------------------------------------------------------------===//

#include "tsan_linux.h"
#include "tsan_rtl.h"
#include "tsan_interface.h"
#include "tsan_atomic.h"
#include "tsan_suppressions.h"
#include "tsan_symbolize.h"
#include "tsan_sync.h"
#include "tsan_report.h"
#include <stddef.h>  // NULL

namespace __tsan {

__thread __tsan::ThreadState cur_thread;

union Shadow {
  struct {
    u64 tid   : kTidBits;
    u64 epoch : kClkBits;
    u64 addr0 : 3;
    u64 addr1 : 3;
    u64 write : 1;
  };
  u64 raw;
};

struct Context {
  Mutex mtx;
  int thread_seq;
  SlabAlloc* clockslab;
  SyncTab *synctab;
  ThreadState *threads[kMaxTid];
};

static Context *ctx;

u64 min(u64 a, u64 b) {
  return a < b ? a : b;
}

u64 max(u64 a, u64 b) {
  return a > b ? a : b;
}

void CheckFailed(const char *file, int line, const char *cond) {
  Report("FATAL: ThreadSanitizer CHECK failed: %s:%d \"%s\"\n",
         file, line, cond);
  Die();
}

static void TraceInit(ThreadState *thr) {
  thr->trace.mtx = new Mutex;
}

static void NOINLINE TraceSwitch(ThreadState *thr) {
  Lock l(thr->trace.mtx);
  int trace = (thr->fast.epoch / (kTraceSize / kTraceParts)) % kTraceParts;
  thr->trace.headers[trace].epoch0 = thr->fast.epoch;
}

static void ALWAYS_INLINE TraceAddEvent(ThreadState *thr, u64 epoch,
                                        EventType typ, uptr addr) {
  if (UNLIKELY((epoch % (kTraceSize / kTraceParts)) == 0))
    TraceSwitch(thr);
  Event *evp = &thr->trace.events[epoch % kTraceSize];
  Event ev = (u64)addr | ((u64)typ << 61);
  *evp = ev;
}

void Initialize() {
  if (TSAN_DEBUG)
    Printf("tsan::Initialize\n");
  InitializeShadowMemory();
  ctx = new Context;
  ctx->clockslab = new SlabAlloc(ChunkedClock::kChunkSize);
  ctx->synctab = new SyncTab;
  InitializeInterceptors();
  InitializeSuppressions();

  // Initialize thread 0.
  ThreadStart(0);
}

int ThreadCreate() {
  Lock l(&ctx->mtx);
  return ++ctx->thread_seq;
}

void ThreadStart(ThreadState *thr, int tid) {
  internal_memset(thr, 0, sizeof(*thr));
  thr->fast.tid = tid;
  thr->clockslab = new SlabCache(ctx->clockslab);
  thr->clock.tick(tid);
  thr->fast.epoch = 1;
  thr->fast_synch_epoch = 1;
  TraceInit(thr);
  ctx->threads[tid] = thr;
}

void ThreadStart(int tid) {
  ThreadStart(&cur_thread, tid);
}

void MutexCreate(ThreadState *thr, uptr addr, bool is_rw) {
  if (TSAN_DEBUG)
    Printf("#%d: MutexCreate %p\n", thr->fast.tid, addr);
  ctx->synctab->insert(new MutexVar(addr, is_rw));
}

void MutexDestroy(ThreadState *thr, uptr addr) {
  if (TSAN_DEBUG)
    Printf("#%d: MutexDestroy %p\n", thr->fast.tid, addr);
  SyncVar *s = ctx->synctab->get_and_remove(addr);
  CHECK(s && s->type == SyncVar::Mtx);
  s->clock.Free(thr->clockslab);
  delete s;
}

void MutexLock(ThreadState *thr, uptr addr) {
  if (TSAN_DEBUG)
    Printf("#%d: MutexLock %p\n", thr->fast.tid, addr);
  thr->fast.epoch++;
  TraceAddEvent(thr, thr->fast.epoch, EventTypeLock, addr);
  SyncVar *s = ctx->synctab->get_and_lock(addr);
  CHECK(s && s->type == SyncVar::Mtx);
  MutexVar *m = static_cast<MutexVar*>(s);
  thr->clock.acquire(&m->clock);
  m->mtx.Unlock();
}

void MutexUnlock(ThreadState *thr, uptr addr) {
  if (TSAN_DEBUG)
    Printf("#%d: MutexUnlock %p\n", thr->fast.tid, addr);
  thr->fast.epoch++;
  TraceAddEvent(thr, thr->fast.epoch, EventTypeUnlock, addr);
  SyncVar *s = ctx->synctab->get_and_lock(addr);
  CHECK(s && s->type == SyncVar::Mtx);
  MutexVar *m = static_cast<MutexVar*>(s);
  thr->clock.release(&m->clock, thr->clockslab);
  thr->fast_synch_epoch = thr->fast.epoch;
  m->mtx.Unlock();
}

template<typename T>
static T* alloc(ReportDesc *rep, int n, int *pos) {
  T* p = (T*)(rep->alloc + *pos);
  *pos += n * sizeof(T);
  CHECK(*pos <= (int)sizeof(rep->alloc));
  return p;
}

static int RestoreStack(int tid, u64 epoch, uptr *stack, int n) {
  ThreadState *thr = ctx->threads[tid];
  Lock l(thr->trace.mtx);
  TraceHeader* trace = &thr->trace.headers[
      (epoch / (kTraceSize / kTraceParts)) % kTraceParts];
  if (epoch < trace->epoch0)
    return 0;
  epoch %= (kTraceSize / kTraceParts);
  u64 pos = 0;
  for (u64 i = 0; i <= epoch; i++) {
    Event ev = thr->trace.events[i];
    EventType typ = (EventType)(ev >> 61);
    uptr pc = (uptr)(ev & 0xffffffffffffull);
    if (typ == EventTypeMop) {
      stack[pos] = pc;
    } else if (typ == EventTypeFuncEnter) {
      stack[pos++] = pc;
    } else if (typ == EventTypeFuncExit) {
      pos--;
    }
  }
  pos++;
  for (u64 i = 0; i <= pos / 2; i++) {
    uptr pc = stack[i];
    stack[i] = stack[pos - i - 1];
    stack[pos - i - 1] = pc;
  }
  return pos;
}

static void NOINLINE ReportRace(ThreadState *thr, uptr addr,
                                Shadow s0, Shadow s1) {
  addr &= ~7;
  int alloc_pos = 0;
  ReportDesc rep;
  rep.typ = ReportTypeRace;
  rep.nmop = 2;
  rep.mop = alloc<ReportMop>(&rep, rep.nmop, &alloc_pos);
  for (int i = 0; i < rep.nmop; i++) {
    ReportMop *mop = &rep.mop[i];
    Shadow *s = (i ? &s1 : &s0);
    mop->tid = s->tid;
    mop->addr = addr + s->addr0;
    mop->size = s->addr1 - s->addr0 + 1;
    mop->write = s->write;
    mop->nmutex = 0;
    uptr stack[64];
    mop->stack.cnt = RestoreStack(s->tid, s->epoch, stack,
                                  sizeof(stack)/sizeof(stack[0]));
    if (mop->stack.cnt != 0) {
      mop->stack.entry = alloc<ReportStackEntry>(&rep, mop->stack.cnt,
                                                   &alloc_pos);
      for (int i = 0; i < mop->stack.cnt; i++) {
        ReportStackEntry *ent = &mop->stack.entry[i];
        ent->pc = stack[i];
        ent->pc = stack[i];
        ent->func = alloc<char>(&rep, 1024, &alloc_pos);
        ent->func[0] = 0;
        ent->file = alloc<char>(&rep, 1024, &alloc_pos);
        ent->file[0] = 0;
        ent->line = 0;
        SymbolizeCode(ent->pc, ent->func, 1024, ent->file, 1024, &ent->line);
      }
    }
  }
  rep.loc = 0;
  rep.nthread = 0;
  rep.nmutex = 0;
  if (IsSuppressed(ReportTypeRace, &rep.mop[0].stack))
    return;
  PrintReport(&rep);
}

ALWAYS_INLINE
static Shadow LoadShadow(u64 *p) {
  Shadow s;
  s.raw = atomic_load((atomic_uint64_t*)p, memory_order_relaxed);
  return s;
}

ALWAYS_INLINE
static void StoreShadow(u64 *p, u64 raw) {
  atomic_store((atomic_uint64_t*)p, raw, memory_order_relaxed);
}

ALWAYS_INLINE
static bool MemoryAccess1(ThreadState *thr, ThreadState::Fast fast_state,
                          u64 synch_epoch, Shadow s0, u64 *sp, bool is_write,
                          bool &replaced, Shadow &racy_access) {
  Shadow s = LoadShadow(sp);
  if (s.raw == 0) {
    if (replaced == false) {
      StoreShadow(sp, s0.raw);
      replaced = true;
    }
    return false;
  }
  // is the memory access equal to the previous?
  if (s0.addr0 == s.addr0 && s0.addr1 == s.addr1) {
    // same thread?
    if (s.tid == fast_state.tid) {
      if (s.epoch >= synch_epoch) {
        if (s.write || !is_write) {
          // found a slot that holds effectively the same info
          // (that is, same tid, same sync epoch and same size)
          return true;
        } else {
          StoreShadow(sp, replaced ? 0ull : s0.raw);
          replaced = true;
          return false;
        }
      } else {
        if (!s.write || is_write) {
          StoreShadow(sp, replaced ? 0ull : s0.raw);
          replaced = true;
          return false;
        } else {
          return false;
        }
      }
    } else {
      // happens before?
      if (thr->clock.get(s.tid) >= s.epoch) {
        StoreShadow(sp, replaced ? 0ull : s0.raw);
        replaced = true;
        return false;
      } else if (!s.write && !is_write) {
        return false;
      } else {
        racy_access = s;
        return false;
      }
    }
  // do the memory access intersect?
  } else if (min(s0.addr1, s.addr1) >= max(s0.addr0, s.addr0)) {
    if (s.tid == fast_state.tid)
      return false;
    // happens before?
    if (thr->clock.get(s.tid) >= s.epoch) {
      return false;
    } else if (!s.write && !is_write) {
      return false;
    } else {
      racy_access = s;
      return false;
    }
  }
  // the accesses do not intersect
  return false;
}

ALWAYS_INLINE
void MemoryAccess(ThreadState *thr, uptr pc, uptr addr,
                  int size, bool is_write) {
  StatInc(thr, StatMop);
  u64 *shadow_mem = (u64*)MemToShadow(addr);
  if (TSAN_DEBUG)
    Printf("#%d: tsan::OnMemoryAccess: @%p %p size=%d"
           " is_write=%d shadow_mem=%p\n",
           (int)thr->fast.tid, (void*)pc, (void*)addr,
           (int)size, is_write, shadow_mem);
  DCHECK(IsAppMem(addr));
  DCHECK(IsShadowMem((uptr)shadow_mem));

  ThreadState::Fast fast_state;
  fast_state.raw = thr->fast.raw;  // Copy.
  fast_state.epoch++;
  thr->fast.raw = fast_state.raw;
  TraceAddEvent(thr, fast_state.epoch, EventTypeMop, pc);

  // descriptor of the memory access
  Shadow s0 = { {fast_state.tid, fast_state.epoch,
               addr&7, min((addr&7)+size-1, 7), is_write} };  // NOLINT
  // Is the descriptor already stored somewhere?
  bool replaced = false;
  // Racy memory access. Zero if none.
  Shadow racy_access;
  racy_access.raw = 0;

  // scan all the shadow values and dispatch to 4 categories:
  // same, replace, candidate and race (see comments below).
  // we consider only 3 cases regarding access sizes:
  // equal, intersect and not intersect. initially I considered
  // larger and smaller as well, it allowed to replace some
  // 'candidates' with 'same' or 'replace', but I think
  // it's just not worth it (performance- and complexity-wise).
  const u64 synch_epoch = thr->fast_synch_epoch;

  // The idea behind the offset is as follows.
  // Consider that we have 8 bool's contained within a single 8-byte block
  // (mapped to a single shadow "cell"). Now consider that we write to the bools
  // from a single thread (which we consider the common case).
  // W/o offsetting each access will have to scan 4 shadow values at average
  // to find the corresponding shadow value for the bool.
  // With offsetting we start scanning shadow with the offset so that
  // each access hits necessary shadow straight off (at least in an expected
  // optimistic case).
  // This logic works seamlessly for any layout of user data. For example,
  // if user data is {int, short, char, char}, then accesses to the int are
  // offsetted to 0, short - 4, 1st char - 6, 2nd char - 7. Hopefully, accesses
  // from a single thread won't need to scan all 8 shadow values.
  int off = 0;
  if (size == 1)
    off = addr & 7;
  else if (size == 2)
    off = addr & 6;
  else if (size == 4)
    off = addr & 4;

  for (int i = 0; i < kShadowCnt; i++) {
    u64 *sp = &shadow_mem[(i + off) % kShadowCnt];
    if (MemoryAccess1(thr, fast_state, synch_epoch, s0, sp, is_write,
                      replaced, racy_access))
      return;
  }

  // find some races?
  if (UNLIKELY(racy_access.raw != 0))
    ReportRace(thr, addr, s0, racy_access);
  // we did not find any races and had already stored
  // the current access info, so we are done
  if (LIKELY(replaced))
    return;
  // choose a random candidate slot and replace it
  unsigned i = fast_state.epoch % kShadowCnt;
  StoreShadow(shadow_mem+i, s0.raw);
}

void FuncEntry(ThreadState *thr, uptr pc) {
  StatInc(thr, StatFuncEnter);
  if (TSAN_DEBUG)
    Printf("#%d: tsan::FuncEntry %p\n", (int)thr->fast.tid, (void*)pc);
  thr->fast.epoch++;
  TraceAddEvent(thr, thr->fast.epoch, EventTypeFuncEnter, pc);
}

void FuncExit(ThreadState *thr) {
  StatInc(thr, StatFuncExit);
  if (TSAN_DEBUG)
    Printf("#%d: tsan::FuncExit\n", (int)thr->fast.tid);
  thr->fast.epoch++;
  TraceAddEvent(thr, thr->fast.epoch, EventTypeFuncExit, 0);
}

}  // namespace __tsan

// Must be included in this file to make sure everything is inlined.
#include "tsan_interface_inl.h"
