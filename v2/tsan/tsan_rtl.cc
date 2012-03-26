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

#include "tsan_platform.h"
#include "tsan_rtl.h"
#include "tsan_interface.h"
#include "tsan_atomic.h"
#include "tsan_placement_new.h"
#include "tsan_suppressions.h"
#include "tsan_symbolize.h"
#include "tsan_sync.h"
#include "tsan_report.h"

namespace __tsan {

__thread char cur_thread_placeholder[sizeof(ThreadState)] ALIGN(64);
static char ctx_placeholder[sizeof(Context)] ALIGN(64);
static ReportDesc g_report;

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

u64 min(u64 a, u64 b) {
  return a < b ? a : b;
}

u64 max(u64 a, u64 b) {
  return a > b ? a : b;
}

Context *ctx;

void CheckFailed(const char *file, int line, const char *cond) {
  Report("FATAL: ThreadSanitizer CHECK failed: %s:%d \"%s\"\n",
         file, line, cond);
  Die();
}

void TraceSwitch(ThreadState *thr) {
  Lock l(&thr->trace.mtx);
  int trace = (thr->epoch / (kTraceSize / kTraceParts)) % kTraceParts;
  thr->trace.headers[trace].epoch0 = thr->epoch;
}

Context::Context()
  : clockslab(SyncClock::kChunkSize)
  , syncslab(sizeof(SyncVar))
  , nreported() {
}

ThreadState::ThreadState(Context *ctx, int tid)
  : tid(tid)
  , clockslab(&ctx->clockslab)
  , syncslab(&ctx->syncslab) {
}

ThreadContext::ThreadContext(int tid)
  : tid(tid)
  , thr()
  , status(ThreadStatusInvalid)
  , uid()
  , detached()
  , reuse_count()
  , epoch0()
  , dead_next() {
}

void Initialize(ThreadState *thr) {
  // Thread safe because done before all threads exist.
  if (ctx)
    return;
  InitializeInterceptors();
  InitializePlatform();
  InitializeDynamicAnnotations();
  ctx = new(ctx_placeholder) Context;
  InitializeShadowMemory();
  ctx->dead_list_size = 0;
  ctx->dead_list_head = 0;
  ctx->dead_list_tail = 0;
  InitializeSuppressions();

  // Initialize thread 0.
  ctx->thread_seq = 0;
  int tid = ThreadCreate(thr, 0, 0, true);
  CHECK_EQ(tid, 0);
  ThreadStart(thr, tid);
}

int Finalize(ThreadState *thr) {
  if (ctx->nreported)
    Printf("ThreadSanitizer summary: reported %d warnings\n", ctx->nreported);

  if (kCollectStats) {
    for (int i = 0; i < StatCnt; i++)
      ctx->stat[i] += thr->stat[i];
    PrintStats(ctx->stat);
  }

  return ctx->nreported ? 66 : 0;
}

static int RestoreStack(int tid, u64 epoch, uptr *stack, int n) {
  Lock l0(&ctx->thread_mtx);
  ThreadContext *tctx = ctx->threads[tid];
  if (tctx == 0)
    return 0;
  Trace* trace = 0;
  if (tctx->status == ThreadStatusRunning) {
    CHECK(tctx->thr);
    trace = &tctx->thr->trace;
  } else if (tctx->status == ThreadStatusFinished
      || tctx->status == ThreadStatusDead) {
    trace = &tctx->dead_info.trace;
  } else {
    return 0;
  }
  Lock l(&trace->mtx);
  TraceHeader* hdr = &trace->headers[
      (epoch / (kTraceSize / kTraceParts)) % kTraceParts];
  if (epoch < hdr->epoch0)
    return 0;
  epoch %= (kTraceSize / kTraceParts);
  u64 pos = 0;
  for (u64 i = 0; i <= epoch; i++) {
    Event ev = trace->events[i];
    EventType typ = (EventType)(ev >> 61);
    uptr pc = (uptr)(ev & 0xffffffffffffull);
    if (typ == EventTypeMop) {
      stack[pos] = pc;
    } else if (typ == EventTypeFuncEnter) {
      stack[pos++] = pc;
    } else if (typ == EventTypeFuncExit) {
      if (pos > 0)
        pos--;
    }
  }
  pos++;
  for (u64 i = 0; i < pos / 2; i++) {
    uptr pc = stack[i];
    stack[i] = stack[pos - i - 1];
    stack[pos - i - 1] = pc;
  }
  return pos;
}

static void NOINLINE ReportRace(ThreadState *thr, uptr addr,
                                Shadow s0, Shadow s1) {
  const int kStackMax = 64;

  if (IsExceptReport(addr))
    return;

  Lock l(&ctx->report_mtx);
  addr &= ~7;
  RegionAlloc alloc(g_report.alloc, sizeof(g_report.alloc));
  ReportDesc &rep = g_report;
  rep.typ = ReportTypeRace;
  rep.nmop = 2;
  rep.mop = alloc.Alloc<ReportMop>(rep.nmop);
  for (int i = 0; i < rep.nmop; i++) {
    ReportMop *mop = &rep.mop[i];
    Shadow *s = (i ? &s1 : &s0);
    mop->tid = s->tid;
    mop->addr = addr + s->addr0;
    mop->size = s->addr1 - s->addr0 + 1;
    mop->write = s->write;
    mop->nmutex = 0;
    mop->stack.cnt = 0;
    uptr stack[kStackMax];
    int stackcnt = RestoreStack(s->tid, s->epoch, stack, kStackMax);
    if (stackcnt != 0) {
      mop->stack.entry = alloc.Alloc<ReportStackEntry>(kStackMax);
      for (int si = 0; si < stackcnt; si++) {
        Symbol symb[kStackMax];
        int framecnt = SymbolizeCode(&alloc, stack[si], symb, kStackMax);
        if (framecnt) {
          for (int fi = 0; fi < framecnt && mop->stack.cnt < kStackMax; fi++) {
            ReportStackEntry *ent = &mop->stack.entry[mop->stack.cnt++];
            ent->pc = stack[si];
            ent->func = symb[fi].name;
            ent->file = symb[fi].file;
            ent->line = symb[fi].line;
          }
        } else if (mop->stack.cnt < kStackMax) {
          ReportStackEntry *ent = &mop->stack.entry[mop->stack.cnt++];
          ent->pc = stack[si];
          ent->func = 0;
          ent->file = 0;
          ent->line = 0;
        }
      }
    }
  }
  rep.loc = 0;
  rep.nthread = 0;
  rep.nmutex = 0;
  bool suppressed = IsSuppressed(ReportTypeRace, &rep.mop[0].stack);
  suppressed = OnReport(&rep, suppressed);
  if (suppressed)
    return;
  PrintReport(&rep);
  ctx->nreported++;
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

template<int kAccessSizeLog, int kAccessIsWrite>
ALWAYS_INLINE
static bool MemoryAccess1(ThreadState *thr, int tid, u64 epoch,
                          u64 synch_epoch, Shadow s0, u64 *sp,
                          bool &replaced, Shadow &racy_access) {
  Shadow s = LoadShadow(sp);
  if (s.raw == 0) {
    StatInc(thr, StatShadowZero);
    if (replaced == false) {
      StoreShadow(sp, s0.raw);
      replaced = true;
    }
    return false;
  }
  // is the memory access equal to the previous?
  if (s0.addr0 == s.addr0 && s0.addr1 == s.addr1) {
    StatInc(thr, StatShadowSameSize);
    // same thread?
    if (s.tid == tid) {
      StatInc(thr, StatShadowSameThread);
      if (s.epoch >= synch_epoch) {
        if (s.write || !kAccessIsWrite) {
          // found a slot that holds effectively the same info
          // (that is, same tid, same sync epoch and same size)
          return true;
        } else {
          StoreShadow(sp, replaced ? 0ull : s0.raw);
          replaced = true;
          return false;
        }
      } else {
        if (!s.write || kAccessIsWrite) {
          StoreShadow(sp, replaced ? 0ull : s0.raw);
          replaced = true;
          return false;
        } else {
          return false;
        }
      }
    } else {
      StatInc(thr, StatShadowAnotherThread);
      // happens before?
      if (thr->clock.get(s.tid) >= s.epoch) {
        StoreShadow(sp, replaced ? 0ull : s0.raw);
        replaced = true;
        return false;
      } else if (!s.write && !kAccessIsWrite) {
        return false;
      } else {
        racy_access = s;
        return false;
      }
    }
  // Do the memory access intersect?
  } else if (min(s0.addr1, s.addr1) >= max(s0.addr0, s.addr0)) {
    StatInc(thr, StatShadowIntersect);
    if (s.tid == tid) {
      StatInc(thr, StatShadowSameThread);
      return false;
    }
    StatInc(thr, StatShadowAnotherThread);
    // happens before?
    if (thr->clock.get(s.tid) >= s.epoch) {
      return false;
    } else if (!s.write && !kAccessIsWrite) {
      return false;
    } else {
      racy_access = s;
      return false;
    }
  // The accesses do not intersect.
  } else {
    StatInc(thr, StatShadowNotIntersect);
  }
  return false;
}

template<u64 kAccessSizeLog, u64 kAccessIsWrite>
ALWAYS_INLINE
bool MemoryAccess(ThreadState *thr, uptr pc, uptr addr) {
  CHECK_LE(kAccessIsWrite, 1);
  CHECK_LE(kAccessSizeLog, 3);
  int kAccessSize = 1 << kAccessSizeLog;
  StatInc(thr, StatMop);
  u64 *shadow_mem = (u64*)MemToShadow(addr);
  DPrintf("#%d: tsan::OnMemoryAccess: @%p %p size=%d"
          " is_write=%d shadow_mem=%p\n",
          (int)thr->tid, (void*)pc, (void*)addr,
          (int)kAccessSize, kAccessIsWrite, shadow_mem);
  DCHECK(IsAppMem(addr));
  DCHECK(IsShadowMem((uptr)shadow_mem));

  int tid = thr->tid;
  u64 epoch = thr->epoch + 1;
  thr->epoch = epoch;
  TraceAddEvent(thr, epoch, EventTypeMop, pc);

  StatInc(thr, kAccessIsWrite ? StatMopWrite : StatMopRead);
  StatInc(thr, kAccessSize == 1 ? StatMop1 : kAccessSize == 2 ? StatMop2
          : kAccessSize == 4 ? StatMop4 : StatMop8);

  // descriptor of the memory access
  Shadow s0 = { {tid, epoch,
               addr&7, min((addr&7)+kAccessSize-1, 7), // NOLINT
               kAccessIsWrite} };  // NOLINT
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
  if (kAccessSize == 1)
    off = addr & 7;
  else if (kAccessSize == 2)
    off = addr & 6;
  else if (kAccessSize == 4)
    off = addr & 4;

  for (int i = 0; i < kShadowCnt; i++) {
    StatInc(thr, StatShadowProcessed);
    u64 *sp = &shadow_mem[(i + off) % kShadowCnt];
    if (MemoryAccess1<kAccessSizeLog, kAccessIsWrite>(thr, tid, epoch,
                                                      synch_epoch, s0, sp,
                                                      replaced, racy_access))
      return false;
  }

  // find some races?
  if (UNLIKELY(racy_access.raw != 0))
    ReportRace(thr, addr, s0, racy_access);
  // we did not find any races and had already stored
  // the current access info, so we are done
  if (LIKELY(replaced))
    return racy_access.raw != 0;
  // choose a random candidate slot and replace it
  unsigned i = epoch % kShadowCnt;
  StoreShadow(shadow_mem+i, s0.raw);
  StatInc(thr, StatShadowReplace);
  return racy_access.raw != 0;
}

template<int kAccessIsWrite>
static void MemoryAccessRange(ThreadState *thr, uptr pc, uptr addr,
                              uptr size) {
  // Handle unaligned beginning, if any.
  for (; addr % 8 && size; addr++, size--) {
    if (MemoryAccess<0, kAccessIsWrite>(thr, pc, addr))
      return;
  }
  // Handle middle part, if any.
  for (; size >= 8; addr += 8, size -= 8) {
    if (MemoryAccess<2, kAccessIsWrite>(thr, pc, addr))
      return;
  }
  // Handle ending, if any.
  for (; size; addr++, size--) {
    if (MemoryAccess<0, kAccessIsWrite>(thr, pc, addr))
      return;
  }
}

void MemoryAccessRange(ThreadState *thr, uptr pc, uptr addr,
                       uptr size, bool is_write) {
  if (is_write)
    MemoryAccessRange<1>(thr, pc, addr, size);
  else
    MemoryAccessRange<0>(thr, pc, addr, size);
}

void MemoryRead1Byte(ThreadState *thr, uptr pc, uptr addr) {
  MemoryAccess<0, 0>(thr, pc, addr);
}

void MemoryWrite1Byte(ThreadState *thr, uptr pc, uptr addr) {
  MemoryAccess<0, 1>(thr, pc, addr);
}

void MemoryResetRange(ThreadState *thr, uptr pc, uptr addr, uptr size) {
  CHECK_EQ(addr % 8, 0);
  (void)thr;
  (void)pc;
  // Some programs mmap like hundreds of GBs but actually used a small part.
  // So, it's better to report a false positive on the memory
  // then to hang here senselessly.
  const uptr kMaxResetSize = 1024*1024*1024;
  if (size > kMaxResetSize)
    size = kMaxResetSize;
  u64 *p = (u64*)MemToShadow(addr);
  // TODO(dvyukov): may overwrite a part outside the region
  for (uptr i = 0; i * 8 < size; i++)
    p[i] = 0;
}

void FuncEntry(ThreadState *thr, uptr pc) {
  StatInc(thr, StatFuncEnter);
  DPrintf("#%d: tsan::FuncEntry %p\n", (int)thr->tid, (void*)pc);
  thr->epoch++;
  TraceAddEvent(thr, thr->epoch, EventTypeFuncEnter, pc);
}

void FuncExit(ThreadState *thr) {
  StatInc(thr, StatFuncExit);
  DPrintf("#%d: tsan::FuncExit\n", (int)thr->tid);
  thr->epoch++;
  TraceAddEvent(thr, thr->epoch, EventTypeFuncExit, 0);
}

}  // namespace __tsan

// Must be included in this file to make sure everything is inlined.
#include "tsan_interface_inl.h"
