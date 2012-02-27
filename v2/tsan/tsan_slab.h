//===-- tsan_slab.h ---------------------------------------------*- C++ -*-===//
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
#ifndef TSAN_SLAB_H
#define TSAN_SLAB_H

#include "tsan_defs.h"
#include "tsan_mutex.h"

namespace __tsan {

class SlabAlloc {
 public:
  explicit SlabAlloc(uptr size);
  ~SlabAlloc();

  void* Alloc(uptr *n);
  void Free(void *first, void *last, uptr n);
  uptr Size() const;

 private:
  Mutex mtx_;
  uptr const size_;
  uptr count_;
  uptr allocated_;
  void **head_;
  void **superblocks_;

  SlabAlloc(const SlabAlloc&);
  void operator = (const SlabAlloc&);
};

class SlabCache {
 public:
  explicit SlabCache(SlabAlloc *parent);
  ~SlabCache();

  void* Alloc();
  void Free(void *p);
  uptr Size() const;

 private:
  SlabAlloc *parent_;
  void **head_;
  uptr count_;

  void* AllocSlow();
  void Drain();

  SlabCache(const SlabCache&);
  void operator = (const SlabCache&);
};

class RegionAlloc {
 public:
  RegionAlloc(void *mem, uptr size);
  void *Alloc(uptr size);

  template<typename T>
  T *Alloc(uptr cnt) {
    return (T*)this->Alloc(cnt * sizeof(T));
  }

 private:
  char *mem_;
  char *end_;

  RegionAlloc(const RegionAlloc&);  // Not implemented.
  void operator = (const RegionAlloc&);  // Not implemented.
};

}  // namespace __tsan

#endif  // TSAN_SLAB_H
