//===-- tsan_clock.cc -------------------------------------------*- C++ -*-===//
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
#include "tsan_clock.h"
#include "tsan_rtl.h"

// It's possible to optimize clock operations for some important cases
// so that they are O(1). The cases include singletons, once's, local mutexes.
// First, SyncClock must be re-implemented to allow indexing by tid.
// It is not necessary must be a full vector clock, though. For example it may
// be a multi-level table.
// Then, each slot in SyncClock must contain a dirty bit (it's united with
// the clock value, so no space increase). The acquire algorithm looks
// as follows:
// void acquire(thr, tid, thr_clock, sync_clock) {
//   if (!sync_clock[tid].dirty)
//     return;  // No new info to acquire.
//              // This handles constant reads of singleton pointers and
//              // stop-flags.
//   acquire_impl(thr_clock, sync_clock);  // As usual, O(N).
//   sync_clock[tid].dirty = false;
//   sync_clock.dirty_count--;
// }
// The release operation looks as follows:
// void release(thr, tid, thr_clock, sync_clock) {
//   // thr->sync_cache is a simple fixed-size hash-based cache that holds
//   // several previous sync_clock's.
//   if (thr->sync_cache[sync_clock] >= thr->last_acquire_epoch) {
//     // The thread did no acquire operations since last release on this clock.
//     // So update only the thread's slot (other slots can't possibly change).
//     sync_clock[tid].clock = thr->epoch;
//     if (sync_clock.dirty_count == sync_clock.cnt
//         || (sync_clock.dirty_count == sync_clock.cnt - 1
//           && sync_clock[tid].dirty == false)
//       // All dirty flags are set, bail out.
//       return;
//     set all dirty bits, but preserve the thread's bit.  // O(N)
//     update sync_clock.dirty_count;
//     return;
//   }
//   release_impl(thr_clock, sync_clock);  // As usual, O(N).
//   set all dirty bits, but preserve the thread's bit.
//   // The previous step is combined with release_impl(), so that
//   // we scan the arrays only once.
//   update sync_clock.dirty_count;
// }

namespace __tsan {

const int kChunkCapacity = SyncClock::kChunkSize / sizeof(u64) - 1;

struct SyncClock::Chunk {
  Chunk* next_;
  u64 clk_[kChunkCapacity];
};

ThreadClock::ThreadClock() {
  nclk_ = 0;
  for (uptr i = 0; i < (uptr)kMaxTid; i++)
    clk_[i] = 0;
}

void ThreadClock::acquire(const SyncClock *src) {
  DCHECK(this->nclk_ <= kMaxTid);
  DCHECK(src->nclk_ <= kMaxTid);

  if (src->nclk_ == 0)
    return;
  if (this->nclk_ < src->nclk_)
    this->nclk_ = src->nclk_;
  SyncClock::Chunk *c = src->chunk_;
  for (int di = 0; c;) {
    for (int si = 0; si < kChunkCapacity && di < this->nclk_;
        si++, di++) {
      if (this->clk_[di] < c->clk_[si])
        this->clk_[di] = c->clk_[si];
    }
    c = c->next_;
  }
}

void ThreadClock::release(SyncClock *dst, SlabCache *slab) const {
  DCHECK((int)slab->Size() == SyncClock::kChunkSize);
  DCHECK(dst->nclk_ <= kMaxTid);
  DCHECK(this->nclk_ <= kMaxTid);

  if (dst->nclk_ < this->nclk_)
    dst->nclk_ = this->nclk_;
  SyncClock::Chunk** cp = &dst->chunk_;
  SyncClock::Chunk* c = *cp;
  for (int si = 0; si < this->nclk_;) {
    if (!c) {
      c = (SyncClock::Chunk*)slab->Alloc();
      c->next_ = 0;
      internal_memset(c->clk_, 0, sizeof(c->clk_));
      *cp = c;
    }
    for (int di = 0; di < kChunkCapacity && si < this->nclk_;
        si++, di++) {
      if (c->clk_[di] < this->clk_[si])
        c->clk_[di] = this->clk_[si];
    }
    cp = &c->next_;
    c = *cp;
  }
}

void ThreadClock::acq_rel(SyncClock *dst, SlabCache *slab) {
  acquire(dst);
  release(dst, slab);
}

SyncClock::SyncClock()
  : nclk_()
  , chunk_() {
  typedef char static_assert_chunk_size[sizeof(Chunk) == kChunkSize ? 1 : -1];
}

SyncClock::~SyncClock() {
  CHECK_EQ(nclk_, 0);
  CHECK_EQ(chunk_, 0);
}

void SyncClock::Free(SlabCache *slab) {
  while (chunk_) {
    Chunk* tmp = chunk_;
    chunk_ = tmp->next_;
    slab->Free(tmp);
  }
  nclk_ =  0;
}

}  // namespace __tsan
