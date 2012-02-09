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

struct ChunkedClock::Chunk {
  Chunk* next_;
  clock_t clk_[ChunkedClock::kChunkSize / sizeof(clock_t) - 1];
};

/*
VectorClock::VectorClock()
  : nclk_() {
    memset(clk_, 0, sizeof(clk_));
}
*/

void VectorClock::acquire(const ChunkedClock *src) {
  DCHECK(this->nclk_ <= kMaxTid);
  DCHECK(src->nclk_ <= kMaxTid);

  if (src->nclk_ == 0)
    return;
  if (this->nclk_ < src->nclk_)
    this->nclk_ = src->nclk_;
  ChunkedClock::Chunk *c = src->chunk_;
  for (int di = 0; di < this->nclk_;) {
    for (int si = 0; si < ChunkedClock::kChunkSize && di < this->nclk_;
        si++, di++) {
      if (this->clk_[di] < c->clk_[si])
        this->clk_[di] = c->clk_[si];
    }
    c = c->next_;
  }
}

void VectorClock::release(ChunkedClock *dst, SlabCache *slab) const {
  DCHECK((int)slab->size() == ChunkedClock::kChunkSize);
  DCHECK(dst->nclk_ <= kMaxTid);
  DCHECK(this->nclk_ <= kMaxTid);

  if (dst->nclk_ < this->nclk_)
    dst->nclk_ = this->nclk_;
  ChunkedClock::Chunk** cp = &dst->chunk_;
  ChunkedClock::Chunk* c = *cp;
  for (int si = 0; si < this->nclk_;) {
    if (!c) {
      c = (ChunkedClock::Chunk*)slab->alloc();
      c->next_ = 0;
      internal_memset(c->clk_, 0, sizeof(c->clk_));
      *cp = c;
    }
    for (int di = 0; di < ChunkedClock::kChunkSize && si < this->nclk_;
        si++, di++) {
      if (c->clk_[di] < this->clk_[si])
        c->clk_[di] = this->clk_[si];
    }
    cp = &c->next_;
    c = *cp;
  }
}

void VectorClock::acq_rel(ChunkedClock *dst, SlabCache *slab) {
  acquire(dst);
  release(dst, slab);
}

ChunkedClock::ChunkedClock()
  : nclk_()
  , chunk_() {
  typedef char static_assert_chunk_size[sizeof(Chunk) == kChunkSize ? 1 : -1];
}

}  // namespace __tsan
