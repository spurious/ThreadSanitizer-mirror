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

#include "tsan_rtl.h"
#include "tsan_placement_new.h"
#include "tsan_platform.h"
#include "tsan_sync.h"

namespace __tsan {

const int kThreadQuarantineSize = 100;

static void ThreadDead(ThreadState *thr, ThreadContext *tctx) {
  CHECK(tctx->status == ThreadStatusRunning
      || tctx->status == ThreadStatusFinished);
  DPrintf("#%d: ThreadDead uid=%lu\n", (int)thr->fast.tid, tctx->uid);
  tctx->status = ThreadStatusDead;
  tctx->uid = 0;
  tctx->sync.Free(&thr->clockslab);

  // Put to dead list.
  tctx->dead_next = 0;
  if (ctx->dead_list_size == 0)
    ctx->dead_list_head = tctx;
  else
    ctx->dead_list_tail->dead_next = tctx;
  ctx->dead_list_tail = tctx;
  ctx->dead_list_size++;
}

int ThreadCreate(ThreadState *thr, uptr uid, bool detached) {
  Lock l(&ctx->thread_mtx);
  int tid = -1;
  ThreadContext *tctx = 0;
  if (ctx->dead_list_size > kThreadQuarantineSize
      || ctx->thread_seq >= kMaxTid) {
    if (ctx->dead_list_size == 0) {
      Printf("ThreadSanitizer: %d thread limit exceeded. Dying.\n", kMaxTid);
      Die();
    }
    tctx = ctx->dead_list_head;
    ctx->dead_list_head = tctx->dead_next;
    ctx->dead_list_size--;
    if (ctx->dead_list_size == 0) {
      CHECK_EQ(tctx->dead_next, 0);
      ctx->dead_list_head = 0;
    }
    CHECK(tctx->status == ThreadStatusDead);
    tctx->status = ThreadStatusInvalid;
    tctx->reuse_count++;
    tid = tctx->tid;
  } else {
    tid = ctx->thread_seq++;
    tctx = new(virtual_alloc(sizeof(ThreadContext))) ThreadContext;
    ctx->threads[tid] = tctx;
    tctx->tid = tid;
    tctx->reuse_count = 0;
  }
  CHECK(tctx != 0 && tid >= 0 && tid < kMaxTid);
  DPrintf("#%d: ThreadCreate tid=%d uid=%lu\n",
          (int)thr->fast.tid, tid, uid);
  CHECK(tctx->status == ThreadStatusInvalid);
  tctx->status = ThreadStatusCreated;
  tctx->thr = 0;
  tctx->uid = uid;
  tctx->detached = detached;
  if (tid) {
    thr->clock.set(thr->fast.tid, thr->fast.epoch);
    thr->fast_synch_epoch = thr->fast.epoch;
    thr->clock.release(&tctx->sync, &thr->clockslab);
  }
  return tid;
}

void ThreadStart(ThreadState *thr, int tid) {
  Lock l(&ctx->thread_mtx);
  ThreadContext *tctx = ctx->threads[tid];
  CHECK(tctx && tctx->status == ThreadStatusCreated);
  tctx->status = ThreadStatusRunning;
  new(thr) ThreadState(ctx);
  tctx->thr = thr;
  thr->fast.tid = tid;
  if (tctx->reuse_count == 0) {
    thr->fast.epoch = 1;
    thr->fast_synch_epoch = 1;
    tctx->epoch0 = 1;
    thr->clock.set(tid, 1);
  } else {
    // Since we reuse the tid, we need to reuse the same clock
    // (the clock can't tick back).
    internal_memcpy(&thr->clock, &tctx->dead_info.clock, sizeof(thr->clock));
    // The point to reclain dead_info.
    // delete tctx->dead_info;
    thr->clock.tick(tid);
    thr->fast.epoch = thr->clock.get(tid);
    thr->fast_synch_epoch = thr->clock.get(tid);
    tctx->epoch0 = thr->clock.get(tid);
  }
  thr->clock.acquire(&tctx->sync);
}

void ThreadFinish(ThreadState *thr) {
  Lock l(&ctx->thread_mtx);
  ThreadContext *tctx = ctx->threads[thr->fast.tid];
  CHECK(tctx && tctx->status == ThreadStatusRunning);
  if (tctx->detached) {
    ThreadDead(thr, tctx);
  } else {
    thr->clock.set(thr->fast.tid, thr->fast.epoch);
    thr->fast_synch_epoch = thr->fast.epoch;
    thr->clock.release(&tctx->sync, &thr->clockslab);
    tctx->status = ThreadStatusFinished;
  }

  // Save from info about the thread.
  // If dead_info will become dynamically allocated again,
  // it is the point to allocate it.
  // tctx->dead_info = new ThreadDeadInfo;
  internal_memcpy(&tctx->dead_info.clock, &thr->clock, sizeof(thr->clock));
  internal_memcpy(&tctx->dead_info.trace, &thr->trace, sizeof(thr->trace));

  thr->~ThreadState();
  tctx->thr = 0;
}

void ThreadJoin(ThreadState *thr, uptr uid) {
  DPrintf("#%d: ThreadJoin uid=%lu\n",
          (int)thr->fast.tid, uid);
  Lock l(&ctx->thread_mtx);
  ThreadContext *tctx = 0;
  int tid = 0;
  for (; tid < kMaxTid; tid++) {
    if (ctx->threads[tid] != 0
        && ctx->threads[tid]->uid == uid
        && ctx->threads[tid]->status != ThreadStatusInvalid) {
      tctx = ctx->threads[tid];
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
  ThreadDead(thr, tctx);
}

void ThreadDetach(ThreadState *thr, uptr uid) {
  Lock l(&ctx->thread_mtx);
  ThreadContext *tctx = 0;
  for (int tid = 0; tid < kMaxTid; tid++) {
    if (ctx->threads[tid] != 0
        && ctx->threads[tid]->uid == uid
        && ctx->threads[tid]->status != ThreadStatusInvalid) {
      tctx = ctx->threads[tid];
      break;
    }
  }
  if (tctx == 0 || tctx->status == ThreadStatusInvalid) {
    Printf("ThreadSanitizer: detach of non-existent thread\n");
    return;
  }
  if (tctx->status == ThreadStatusFinished) {
    ThreadDead(thr, tctx);
  } else {
    tctx->detached = true;
  }
}

}  // namespace __tsan
