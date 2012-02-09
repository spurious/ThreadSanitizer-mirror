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
  explicit SlabAlloc(size_t size);

  void* alloc();
  void free(void *p);
  size_t size() const;

 private:
  Mutex mtx_;
  size_t const size_;

  SlabAlloc(const SlabAlloc&);
  void operator = (const SlabAlloc&);
};

class SlabCache {
 public:
  explicit SlabCache(SlabAlloc *parent);

  void* alloc();
  void free(void *p);
  size_t size() const;

 private:
  SlabAlloc *parent_;

  SlabCache(const SlabCache&);
  void operator = (const SlabCache&);
};

}  // namespace __tsan

#endif  // TSAN_SLAB_H
