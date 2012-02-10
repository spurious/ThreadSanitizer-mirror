//===-- tsan_clock.h --------------------------------------------*- C++ -*-===//
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
#ifndef TSAN_CLOCK_H
#define TSAN_CLOCK_H

#include "tsan_defs.h"
#include "tsan_slab.h"

namespace __tsan {

class ChunkedClock {
 public:
  static const int kChunkSize = 128;
  ChunkedClock();

  int size() const {
    return nclk_;
  }

 private:
  int nclk_;
  struct Chunk;
  Chunk* chunk_;
  friend struct VectorClock;
};

// Has to be POD because lives in TLS.
struct VectorClock {
  void Init();

  clock_t get(int tid) const {
    DCHECK(tid < kMaxTid);
    return clk_[tid];
  }

  void tick(int tid) {
    DCHECK(tid < kMaxTid);
    clk_[tid]++;
    if (nclk_ <= tid)
      nclk_ = tid + 1;
  }

  int size() const {
    return nclk_;
  }

  void acquire(const ChunkedClock *src);
  void release(ChunkedClock *dst, SlabCache *slab) const;
  void acq_rel(ChunkedClock *dst, SlabCache *slab);

// private: Pretend you do not see that.
  int nclk_;
  clock_t clk_[kMaxTid];
};

}  // namespace __tsan

#endif  // TSAN_CLOCK_H
