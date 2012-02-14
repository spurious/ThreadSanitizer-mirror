//===-- tsan_rtl_thread.cc --------------------------------------*- C++ -*-===//
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

static void ThreadFree(ThreadState *thr, ThreadContext *tctx) {
  CHECK(tctx->status == ThreadStatusRunning
      || tctx->status == ThreadStatusFinished);
  if (TSAN_DEBUG)
    Printf("#%d: ThreadFree uid=%lu\n", (int)thr->fast.tid, tctx->uid);
  tctx->status = ThreadStatusInvalid;
  tctx->uid = 0;
  tctx->sync.Free(thr->clockslab);
}

int ThreadCreate(ThreadState *thr, uptr uid, bool detached) {
  Lock l(&ctx->thread_mtx);
  const int tid = ctx->thread_seq++;
  if (TSAN_DEBUG)
    Printf("#%d: ThreadCreate tid=%d uid=%lu\n",
           (int)thr->fast.tid, tid, uid);
  ThreadContext *tctx = &ctx->threads[tid];
  CHECK(tctx->status == ThreadStatusInvalid);
  tctx->status = ThreadStatusCreated;
  tctx->thr = 0;
  tctx->uid = uid;
  tctx->detached = detached;
  if (tid) {
    thr->clock.set(thr->fast.tid, thr->fast.epoch);
    thr->fast_synch_epoch = thr->fast.epoch;
    thr->clock.release(&tctx->sync, thr->clockslab);
  }
  return tid;
}

void ThreadStart(ThreadState *thr, int tid) {
  internal_memset(thr, 0, sizeof(*thr));
  thr->fast.tid = tid;
  thr->clockslab = new SlabCache(ctx->clockslab);
  thr->clock.set(tid, 1);
  thr->fast.epoch = 1;
  thr->fast_synch_epoch = 1;
  thr->trace.mtx = new Mutex;

  {
    Lock l(&ctx->thread_mtx);
    ThreadContext *tctx = &ctx->threads[tid];
    CHECK(tctx->status == ThreadStatusCreated);
    tctx->status = ThreadStatusRunning;
    tctx->thr = thr;
    thr->clock.acquire(&tctx->sync);
  }
}

void ThreadFinish(ThreadState *thr) {
  Lock l(&ctx->thread_mtx);
  ThreadContext *tctx = &ctx->threads[thr->fast.tid];
  CHECK(tctx->status == ThreadStatusRunning);
  if (tctx->detached) {
    ThreadFree(thr, tctx);
  } else {
    thr->clock.set(thr->fast.tid, thr->fast.epoch);
    thr->fast_synch_epoch = thr->fast.epoch;
    thr->clock.release(&tctx->sync, thr->clockslab);
    tctx->status = ThreadStatusFinished;
  }
  tctx->thr = 0;
}

void ThreadJoin(ThreadState *thr, uptr uid) {
  if (TSAN_DEBUG)
    Printf("#%d: ThreadJoin uid=%lu\n",
           (int)thr->fast.tid, uid);
  Lock l(&ctx->thread_mtx);
  ThreadContext *tctx = 0;
  int tid = 0;
  for (; tid < kMaxTid; tid++) {
    if (ctx->threads[tid].uid == uid
        && ctx->threads[tid].status != ThreadStatusInvalid) {
      tctx = &ctx->threads[tid];
      break;
    }
  }
  if (tctx == 0 || tctx->status == ThreadStatusInvalid) {
    Printf("ThreadSanitizer: join of non-existent thread\n");
    return;
  }
  CHECK(tctx->detached == false);
  CHECK(tctx->status == ThreadStatusFinished);
  thr->clock.acquire(&tctx->sync);
  ThreadFree(thr, tctx);
}

void ThreadDetach(ThreadState *thr, uptr uid) {
  Lock l(&ctx->thread_mtx);
  ThreadContext *tctx = 0;
  for (int tid = 0; tid < kMaxTid; tid++) {
    if (ctx->threads[tid].uid == uid) {
      tctx = &ctx->threads[tid];
      break;
    }
  }
  if (tctx == 0 || tctx->status == ThreadStatusInvalid) {
    Printf("ThreadSanitizer: detach of non-existent thread\n");
    return;
  }
  if (tctx->status == ThreadStatusFinished) {
    ThreadFree(thr, tctx);
  } else {
    tctx->detached = true;
  }
}

}  // namespace __tsan
