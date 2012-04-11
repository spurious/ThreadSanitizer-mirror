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
#include "tsan_suppressions.h"
#include "tsan_symbolize.h"
#include "tsan_report.h"
#include "tsan_sync.h"
#include "tsan_mman.h"

namespace __tsan {

ReportDesc *GetGlobalReport() {
  static ReportDesc report;
  return &report;
}

static void RestoreStack(ThreadState *thr, int tid,
                        const u64 epoch, StackTrace *stk) {
  stk->Free(thr);
  ThreadContext *tctx = CTX()->threads[tid];
  if (tctx == 0)
    return;
  Trace* trace = 0;
  if (tctx->status == ThreadStatusRunning) {
    CHECK(tctx->thr);
    trace = &tctx->thr->trace;
  } else if (tctx->status == ThreadStatusFinished
      || tctx->status == ThreadStatusDead) {
    trace = &tctx->dead_info.trace;
  } else {
    return;
  }
  Lock l(&trace->mtx);
  const int partidx = (epoch / (kTraceSize / kTraceParts)) % kTraceParts;
  TraceHeader* hdr = &trace->headers[partidx];
  if (epoch < hdr->epoch0)
    return;
  const u64 eend = epoch % kTraceSize;
  const u64 ebegin = eend / kTracePartSize * kTracePartSize;
  DPrintf("#%d: RestoreStack epoch=%llu ebegin=%llu eend=%llu partidx=%d\n",
      tid, epoch, ebegin, eend, partidx);
  InternalScopedBuf<uptr> stack(1024);  // FIXME: de-hardcode 1024
  for (uptr i = 0; i < hdr->stack0.Size(); i++) {
    stack[i] = hdr->stack0.Get(i);
    DPrintf2("  #%02d: pc=%p\n", i, stack[i]);
  }
  uptr pos = hdr->stack0.Size();
  for (uptr i = ebegin; i <= eend; i++) {
    Event ev = trace->events[i];
    EventType typ = (EventType)(ev >> 61);
    uptr pc = (uptr)(ev & 0xffffffffffffull);
    DPrintf2("  %04llu typ=%d pc=%p\n", i, typ, pc);
    if (typ == EventTypeMop) {
      stack[pos] = pc;
    } else if (typ == EventTypeFuncEnter) {
      stack[pos++] = pc;
    } else if (typ == EventTypeFuncExit) {
      // Since we have full stacks, this should never happen.
      DCHECK_GT(pos, 0);
      if (pos > 0)
        pos--;
    }
    for (uptr j = 0; j <= pos; j++)
      DPrintf2("      #%d: %p\n", j, stack[j]);
  }
  if (pos == 0 && stack[0] == 0)
    return;
  pos++;
  stk->Init(thr, stack, pos);
}

static void StackStripMain(ReportStack *stack) {
  ReportStack *last_frame = 0;
  ReportStack *last_frame2 = 0;
  for (ReportStack *ent = stack; ent; ent = ent->next) {
    last_frame2 = last_frame;
    last_frame = ent;
  }

  if (last_frame2 == 0)
    return;
  const char *last = last_frame->func;
  const char *last2 = last_frame2->func;
  // Strip frame above 'main'
  if (last2 && 0 == internal_strcmp(last2, "main")) {
    last_frame2->next = 0;
  // Strip our internal thread start routine.
  } else if (last && 0 == internal_strcmp(last, "__tsan_thread_start_func")) {
    last_frame2->next = 0;
  // Strip global ctors init.
  } else if (last && 0 == internal_strcmp(last, "__do_global_ctors_aux")) {
    last_frame2->next = 0;
  // If both are 0, then we probably just failed to symbolize.
  } else if (last || last2) {
    // Ensure that we recovered stack completely. Trimmed stack
    // can actually happen if we do not instrument some code,
    // so it's only a DCHECK. However we must try hard to not miss it
    // due to our fault.
    Printf("Bottom stack frame of stack %p is missed\n", stack->pc);
    DCHECK(0);
  }
}

ReportStack *SymbolizeStack(RegionAlloc *alloc, const StackTrace& trace) {
  if (trace.IsEmpty())
    return 0;
  ReportStack *stack = 0;
  for (uptr si = 0; si < trace.Size(); si++) {
    // We obtain the return address, that is, address of the next instruction,
    // so offset it by 1 byte.
    bool is_last = (si == trace.Size() - 1);
    ReportStack *ent = SymbolizeCode(alloc, trace.Get(si) - !is_last);
    CHECK_NE(ent, 0);
    ReportStack *last = ent;
    while (last->next) {
      last->pc += !is_last;
      last = last->next;
    }
    last->pc += !is_last;
    last->next = stack;
    stack = ent;
  }
  StackStripMain(stack);
  return stack;
}

static bool HandleRacy(ThreadState *thr, uptr addr) {
  Context *ctx = CTX();
  for (uptr i = 0; i < ctx->racy_siz; i++) {
    if (ctx->racy_accesses[i] == addr)
      return true;
  }
  if (ctx->racy_siz == ctx->racy_cap) {
    ctx->racy_cap *= 2;
    if (ctx->racy_cap == 0)
      ctx->racy_cap = 32;
    uptr *racy = (uptr*)internal_alloc(ctx->racy_cap * sizeof(*racy));
    if (ctx->racy_accesses) {
      internal_memcpy(racy, ctx->racy_accesses, ctx->racy_siz * sizeof(*racy));
      internal_free(ctx->racy_accesses);
    }
    ctx->racy_accesses = racy;
  }
  ctx->racy_accesses[ctx->racy_siz++] = addr;
  return false;
}

void ReportRace(ThreadState *thr) {
  ScopedInRtl in_rtl;
  uptr addr = ShadowToMem((uptr)thr->racy_shadow_addr);
  {
    uptr a0 = addr + FastState(thr->racy_state[0]).addr0();
    uptr a1 = addr + FastState(thr->racy_state[1]).addr0();
    uptr e0 = a0 + FastState(thr->racy_state[0]).size();
    uptr e1 = a1 + FastState(thr->racy_state[1]).size();
    uptr minaddr = min(a0, a1);
    uptr maxaddr = max(e0, e1);
    if (IsExpectReport(minaddr, maxaddr - minaddr))
      return;
  }

  Lock l0(&CTX()->thread_mtx);
  Lock l1(&CTX()->report_mtx);

  if (HandleRacy(thr, addr + FastState(thr->racy_state[0]).addr0()))
    return;

  ReportDesc &rep = *GetGlobalReport();
  RegionAlloc alloc(rep.alloc, sizeof(rep.alloc));
  rep.typ = ReportTypeRace;
  rep.nmop = 2;
  if (thr->racy_state[1] == kShadowFreed)
    rep.nmop = 1;
  rep.mop = alloc.Alloc<ReportMop>(rep.nmop);
  for (int i = 0; i < rep.nmop; i++) {
    ReportMop *mop = &rep.mop[i];
    FastState s(thr->racy_state[i]);
    mop->tid = s.tid();
    mop->addr = addr + s.addr0();
    mop->size = s.size();
    mop->write = s.is_write();
    mop->nmutex = 0;
    StackTrace trace;
    RestoreStack(thr, s.tid(), s.epoch(), &trace);
    // Ensure that we have at least something for the current thread.
    CHECK(i != 0 || !trace.IsEmpty());
    mop->stack = SymbolizeStack(&alloc, trace);
    trace.Free(thr);
  }
  rep.loc = 0;
  rep.nthread = 2;
  rep.thread = alloc.Alloc<ReportThread>(rep.nthread);
  for (int i = 0; i < rep.nthread; i++) {
    FastState s(thr->racy_state[i]);
    ReportThread *rt = &rep.thread[i];
    rt->id = s.tid();
    rt->running = false;
    rt->name = 0;
    rt->stack = 0;
    if (thr->racy_state[i] == kShadowFreed)
      continue;
    ThreadContext *tctx = CTX()->threads[s.tid()];
    CHECK_NE(tctx, (ThreadContext*)0);
    if (s.epoch() < tctx->epoch0 || s.epoch() > tctx->epoch1)
      continue;
    rt->running = (tctx->status == ThreadStatusRunning);
    rt->stack = SymbolizeStack(&alloc, tctx->creation_stack);
  }
  rep.nmutex = 0;
  bool suppressed = IsSuppressed(ReportTypeRace, rep.mop[0].stack);
  suppressed = OnReport(&rep, suppressed);
  if (suppressed)
    return;
  PrintReport(&rep);
  CTX()->nreported++;
}

void CheckFailed(const char *file, int line, const char *cond, u64 v1, u64 v2) {
  ScopedInRtl in_rtl;
  Report("FATAL: ThreadSanitizer CHECK failed: %s:%d \"%s\" (%llu, %llu)\n",
         file, line, cond, v1, v2);
  ThreadState *thr = cur_thread();
  InternalScopedBuf<char> buf(1024*1024);
  RegionAlloc alloc(buf, buf.Size());
  StackTrace stack;
  stack.ObtainCurrent(thr, 0);
  ReportStack *rstack = SymbolizeStack(&alloc, stack);
  stack.Free(thr);
  PrintStack(rstack);
  Printf("Thread %d\n", thr->tid);
  ThreadContext *tctx = CTX()->threads[thr->tid];
  if (tctx) {
    rstack = SymbolizeStack(&alloc, tctx->creation_stack);
    PrintStack(rstack);
  }
  Die();
}

}  // namespace __tsan
