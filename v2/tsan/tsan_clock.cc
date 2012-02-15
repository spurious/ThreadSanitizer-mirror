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

namespace __tsan {

const int kChunkCapacity = SyncClock::kChunkSize / sizeof(u64) - 1;

struct SyncClock::Chunk {
  Chunk* next_;
  u64 clk_[kChunkCapacity];
};

ThreadClock::ThreadClock() {
  nclk_ = 0;
  for (uptr i = 0; i < kMaxTid; i++)
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
