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
  DPrintf("#%d: ThreadDead uid=%lu\n", (int)thr->fast_state.tid(), tctx->uid);
  tctx->status = ThreadStatusDead;
  tctx->uid = 0;
  tctx->sync.Free(&thr->clockslab);

  // Put to dead list.
  tctx->dead_next = 0;
  if (CTX()->dead_list_size == 0)
    CTX()->dead_list_head = tctx;
  else
    CTX()->dead_list_tail->dead_next = tctx;
  CTX()->dead_list_tail = tctx;
  CTX()->dead_list_size++;
}

int ThreadCreate(ThreadState *thr, uptr pc, uptr uid, bool detached) {
  Lock l(&CTX()->thread_mtx);
  int tid = -1;
  ThreadContext *tctx = 0;
  if (CTX()->dead_list_size > kThreadQuarantineSize
      || CTX()->thread_seq >= kMaxTid) {
    if (CTX()->dead_list_size == 0) {
      Printf("ThreadSanitizer: %d thread limit exceeded. Dying.\n", kMaxTid);
      Die();
    }
    tctx = CTX()->dead_list_head;
    CTX()->dead_list_head = tctx->dead_next;
    CTX()->dead_list_size--;
    if (CTX()->dead_list_size == 0) {
      CHECK_EQ(tctx->dead_next, 0);
      CTX()->dead_list_head = 0;
    }
    CHECK(tctx->status == ThreadStatusDead);
    tctx->status = ThreadStatusInvalid;
    tctx->reuse_count++;
    tid = tctx->tid;
    // The point to reclain dead_info.
    // delete tctx->dead_info;
  } else {
    tid = CTX()->thread_seq++;
    tctx = new(virtual_alloc(sizeof(ThreadContext))) ThreadContext(tid);
    CTX()->threads[tid] = tctx;
  }
  CHECK(tctx != 0 && tid >= 0 && tid < kMaxTid);
  DPrintf("#%d: ThreadCreate tid=%d uid=%lu\n",
          (int)thr->fast_state.tid(), tid, uid);
  CHECK(tctx->status == ThreadStatusInvalid);
  tctx->status = ThreadStatusCreated;
  tctx->thr = 0;
  tctx->uid = uid;
  tctx->detached = detached;
  if (tid) {
    thr->clock.set(thr->fast_state.tid(), thr->fast_state.epoch());
    thr->fast_synch_epoch = thr->fast_state.epoch();
    thr->clock.release(&tctx->sync, &thr->clockslab);
  }
  return tid;
}

void ThreadStart(ThreadState *thr, int tid, uptr stk_top, uptr stk_siz) {
  if (stk_top && stk_siz)
    MemoryResetRange(thr, /*pc=*/ 0, stk_top, stk_siz);
  Lock l(&CTX()->thread_mtx);
  ThreadContext *tctx = CTX()->threads[tid];
  CHECK(tctx && tctx->status == ThreadStatusCreated);
  tctx->status = ThreadStatusRunning;
  tctx->epoch0++;
  new(thr) ThreadState(CTX(), tid, tctx->epoch0, stk_top, stk_siz);
  tctx->thr = thr;
  thr->fast_synch_epoch = tctx->epoch0;
  thr->clock.set(tid, tctx->epoch0);
  thr->clock.acquire(&tctx->sync);
}

void ThreadFinish(ThreadState *thr) {
  // FIXME: Treat it as write.
  if (thr->stk_top && thr->stk_siz)
    MemoryResetRange(thr, /*pc=*/ 0, thr->stk_top, thr->stk_siz);
  Lock l(&CTX()->thread_mtx);
  ThreadContext *tctx = CTX()->threads[thr->fast_state.tid()];
  CHECK(tctx && tctx->status == ThreadStatusRunning);
  if (tctx->detached) {
    ThreadDead(thr, tctx);
  } else {
    thr->clock.set(thr->fast_state.tid(), thr->fast_state.epoch());
    thr->fast_synch_epoch = thr->fast_state.epoch();
    thr->clock.release(&tctx->sync, &thr->clockslab);
    tctx->status = ThreadStatusFinished;
  }

  // Save from info about the thread.
  // If dead_info will become dynamically allocated again,
  // it is the point to allocate it.
  // tctx->dead_info = new ThreadDeadInfo;
  internal_memcpy(&tctx->dead_info.trace, &thr->trace, sizeof(thr->trace));
  tctx->epoch0 = thr->clock.get(tctx->tid);

  if (kCollectStats) {
    for (int i = 0; i < StatCnt; i++)
      CTX()->stat[i] += thr->stat[i];
  }

  thr->~ThreadState();
  tctx->thr = 0;
}

void ThreadJoin(ThreadState *thr, uptr pc, uptr uid) {
  DPrintf("#%d: ThreadJoin uid=%lu\n",
          (int)thr->fast_state.tid(), uid);
  Lock l(&CTX()->thread_mtx);
  ThreadContext *tctx = 0;
  int tid = 0;
  for (; tid < kMaxTid; tid++) {
    if (CTX()->threads[tid] != 0
        && CTX()->threads[tid]->uid == uid
        && CTX()->threads[tid]->status != ThreadStatusInvalid) {
      tctx = CTX()->threads[tid];
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

void ThreadDetach(ThreadState *thr, uptr pc, uptr uid) {
  Lock l(&CTX()->thread_mtx);
  ThreadContext *tctx = 0;
  for (int tid = 0; tid < kMaxTid; tid++) {
    if (CTX()->threads[tid] != 0
        && CTX()->threads[tid]->uid == uid
        && CTX()->threads[tid]->status != ThreadStatusInvalid) {
      tctx = CTX()->threads[tid];
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
