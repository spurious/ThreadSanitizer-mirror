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
#include "tsan_sync.h"
#include "tsan_report.h"

namespace __tsan {

static __thread __tsan::ThreadState cur_thread;

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

static struct {
  int thread_seq;
  SlabAlloc* clockslab;
  SyncTab *synctab;
} ctx;

u64 min(u64 a, u64 b) {
  return a < b ? a : b;
}

u64 max(u64 a, u64 b) {
  return a > b ? a : b;
}

static unsigned fastrand(ThreadState *thr) {
  return thr->rand = thr->rand * 1103515245 + 12345;
}

void CheckFailed(const char *file, int line, const char *cond) {
  Report("FATAL: ThreadSanitizer CHECK failed: %s:%d \"%s\"\n",
         file, line, cond);
  Die();
}

void Initialize() {
  Printf("tsan::Initialize\n");
  InitializeShadowMemory();
  ctx.clockslab = new SlabAlloc(ChunkedClock::kChunkSize);
  ctx.synctab = new SyncTab;
  InitializeInterceptors();
  InitializeSuppressions();
}

int ThreadCreate() {
  return ++ctx.thread_seq;
}

void ThreadStart(ThreadState *thr, int tid) {
  internal_memset(thr, 0, sizeof(*thr));
  thr->fast.tid = tid;
  thr->clockslab = new SlabCache(ctx.clockslab);
  thr->clock.tick(tid);
  thr->fast.epoch = 1;
  thr->trace = new TraceSet;
  internal_memset(thr->trace, 0, sizeof(*thr->trace));
}

void ThreadStart(int tid) {
  ThreadStart(&cur_thread, tid);
}

void MutexCreate(ThreadState *thr, uptr addr, bool is_rw) {
  Printf("#%d: MutexCreate %p\n", thr->fast.tid, addr);
  ctx.synctab->insert(new MutexVar(addr, is_rw));
}

void MutexDestroy(ThreadState *thr, uptr addr) {
  Printf("#%d: MutexDestroy %p\n", thr->fast.tid, addr);
  SyncVar *s = ctx.synctab->get_and_lock(addr);
  CHECK(s && s->type == SyncVar::Mtx);
  ctx.synctab->remove(s);
  s->mtx.Unlock();
  delete s;
}

void MutexLock(ThreadState *thr, uptr addr) {
  Printf("#%d: MutexLock %p\n", thr->fast.tid, addr);
  thr->trace->AddEvent(EventTypeLock, addr);
  SyncVar *s = ctx.synctab->get_and_lock(addr);
  CHECK(s && s->type == SyncVar::Mtx);
  MutexVar *m = static_cast<MutexVar*>(s);
  thr->clock.acquire(&m->clock);
  m->mtx.Unlock();
}

void MutexUnlock(ThreadState *thr, uptr addr) {
  Printf("#%d: MutexUnlock %p\n", thr->fast.tid, addr);
  thr->trace->AddEvent(EventTypeUnlock, addr);
  SyncVar *s = ctx.synctab->get_and_lock(addr);
  CHECK(s && s->type == SyncVar::Mtx);
  MutexVar *m = static_cast<MutexVar*>(s);
  thr->clock.release(&m->clock, thr->clockslab);
  m->mtx.Unlock();
}

template<typename T>
static T* alloc(ReportDesc *rep, int n, int *pos) {
  T* p = (T*)(rep->alloc + *pos);
  *pos += n * sizeof(T);
  CHECK(*pos <= (int)sizeof(rep->alloc));
  return p;
}

static void NOINLINE ReportRace(ThreadState *thr, uptr addr,
                                Shadow s0, Shadow s1) {
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
    mop->stack.cnt = 0;
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
void MemoryAccess(ThreadState *thr, uptr pc, uptr addr,
                  int size, bool is_write) {
  u64 *shadow_mem = (u64*)MemToShadow(addr);
  if (TSAN_DEBUG)
    Printf("#%d: tsan::OnMemoryAccess: @%p %p size=%d"
           " is_write=%d shadow_mem=%p\n",
           (int)thr->fast.tid, (void*)pc, (void*)addr,
           size, is_write, shadow_mem);
  DCHECK(IsAppMem(addr));
  DCHECK(IsShadowMem((uptr)shadow_mem));

  // FIXME. We should not be doing this on every access.
  thr->trace->AddEvent(EventTypeMop, pc);

  ThreadState::Fast fast_state;
  fast_state.raw = thr->fast.raw;  // Copy.

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
  for (int i = 0; i < kShadowCnt; i++) {
    u64 *sp = &shadow_mem[i];
    Shadow s = LoadShadow(sp);
    // Printf("  [%d] %llx\n", i, s.raw);
    if (s.raw == 0) {
      if (replaced == false) {
        StoreShadow(sp, s0.raw);
        replaced = true;
      }
      continue;
    }
    // is the memory access equal to the previous?
    if (s0.addr0 == s.addr0 && s0.addr1 == s.addr1) {
      // same thread?
      if (s.tid == fast_state.tid) {
        if (s.epoch >= fast_state.epoch) {
          if (s.write || !is_write) {
            // found a slot that holds effectively the same info
            // (that is, same tid, same sync epoch and same size)
            return;
          } else {
            StoreShadow(sp, replaced ? 0ull : s0.raw);
            replaced = true;
            continue;
          }
        } else {
          if (!s.write || is_write) {
            StoreShadow(sp, replaced ? 0ull : s0.raw);
            replaced = true;
            continue;
          } else {
            continue;
          }
        }
      } else {
        // happens before?
        if (thr->clock.get(s.tid) >= s.epoch) {
          StoreShadow(sp, replaced ? 0ull : s0.raw);
          replaced = true;
          continue;
        } else if (!s.write && !is_write) {
          continue;
        } else {
          racy_access = s;
          continue;
        }
      }
    // do the memory access intersect?
    } else if (min(s0.addr1, s.addr1) >= max(s0.addr0, s.addr0)) {
      if (s.tid == fast_state.tid)
        continue;
      // happens before?
      if (thr->clock.get(s.tid) >= s.epoch) {
        continue;
      } else if (!s.write && !is_write) {
        continue;
      } else {
        racy_access = s;
        continue;
      }
    }
    // the accesses do not intersect
    continue;
  }

  // find some races?
  if (UNLIKELY(racy_access.raw != 0))
    ReportRace(thr, addr, s0, racy_access);
  // we did not find any races and had already stored
  // the current access info, so we are done
  if (replaced)
    return;
  // choose a random candidate slot and replace it
  unsigned i = fastrand(thr) % kShadowCnt;
  StoreShadow(shadow_mem+i, s0.raw);
}

void FuncEntry(ThreadState *thr, uptr pc) {
  Printf("#%d: tsan::FuncEntry %p\n", (int)thr->fast.tid, (void*)pc);
  thr->trace->AddEvent(EventTypeFuncEnter, pc);
}

void FuncExit(ThreadState *thr) {
  Printf("#%d: tsan::FuncExit\n", (int)thr->fast.tid);
  thr->trace->AddEvent(EventTypeFuncExit, 0);
}

}  // namespace __tsan

// Must be included in this file to make sure everything is inlined.
#include "tsan_interface_inl.h"
