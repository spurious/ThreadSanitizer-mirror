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
#include "tsan_mman.h"

/*
#include "tsan_interface.h"
#include "tsan_atomic.h"
#include "tsan_placement_new.h"
#include "tsan_sync.h"
*/

namespace __tsan {

ReportDesc *GetGlobalReport() {
  static ReportDesc report;
  return &report;
}

static int RestoreStack(int tid, const u64 epoch, uptr *stack, int n) {
  ThreadContext *tctx = CTX()->threads[tid];
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
  const int partidx = (epoch / (kTraceSize / kTraceParts)) % kTraceParts;
  TraceHeader* hdr = &trace->headers[partidx];
  if (epoch < hdr->epoch0)
    return 0;
  u64 pos = 0;
  const u64 eend = epoch % kTraceSize;
  const u64 ebegin = eend / kTracePartSize * kTracePartSize;
  DPrintf("#%d: RestoreStack epoch=%llu ebegin=%llu eend=%llu partidx=%d\n",
      tid, epoch, ebegin, eend, partidx);
  for (u64 i = ebegin; i <= eend; i++) {
    Event ev = trace->events[i];
    EventType typ = (EventType)(ev >> 61);
    uptr pc = (uptr)(ev & 0xffffffffffffull);
    DPrintf2("  %04llu typ=%d pc=%p\n", i, typ, pc);
    if (typ == EventTypeMop) {
      stack[pos] = pc;
    } else if (typ == EventTypeFuncEnter) {
      stack[pos++] = pc;
    } else if (typ == EventTypeFuncExit) {
      if (pos > 0)
        pos--;
    }
  }
  if (pos == 0 && stack[0] == 0)
    return 0;
  pos++;
  return pos;
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
    Printf("Top stack frame (main or __tsan_thread_start_func) missed\n");
  }
}

ReportStack *SymbolizeStack(RegionAlloc *alloc, const uptr *pcs, int cnt) {
  if (cnt == 0)
    return 0;
  ReportStack *stack = 0;
  for (int si = 0; si < cnt; si++) {
    // We obtain the return address, that is, address of the next instruction,
    // so offset it by 1 byte.
    bool is_last = (si == cnt - 1);
    ReportStack *ent = SymbolizeCode(alloc, pcs[si] - !is_last);
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

void ReportRace(ThreadState *thr) {
  const int kStackMax = 64;

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
    InternalScopedBuf<uptr> stack(kStackMax);
    int stackcnt = RestoreStack(s.tid(), s.epoch(), stack, stack.Count());
    // Ensure that we have at least something for the current thread.
    CHECK(i != 0 || stackcnt != 0);
    mop->stack = SymbolizeStack(&alloc, stack, stackcnt);
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
    rt->stack = SymbolizeStack(&alloc,
        tctx->creation_stack.Begin(), tctx->creation_stack.Size());
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
  ReportStack *rstack = SymbolizeStack(&alloc, stack.Begin(), stack.Size());
  PrintStack(rstack);
  Printf("Thread %d\n", thr->tid);
  ThreadContext *tctx = CTX()->threads[thr->tid];
  if (tctx) {
    rstack = SymbolizeStack(&alloc,
      tctx->creation_stack.Begin(), tctx->creation_stack.Size());
    PrintStack(rstack);
  }
  Die();
}

}  // namespace __tsan
