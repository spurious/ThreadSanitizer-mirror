//===-- tsan_slab.cc --------------------------------------------*- C++ -*-===//
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
#include "tsan_slab.h"

namespace __tsan {

static LowLevelAllocator allocator;

SlabAlloc::SlabAlloc(size_t size)
  : size_(size) {
}

void* SlabAlloc::alloc() {
  return allocator.Allocate(size_);
}

void SlabAlloc::free(void* p) {
  CHECK(0);
  // FIXME: use LowLevelAllocator
}

size_t SlabAlloc::size() const {
  return size_;
}

SlabCache::SlabCache(SlabAlloc* parent)
  : parent_(parent) {
}

void* SlabCache::alloc() {
  return parent_->alloc();
}

void SlabCache::free(void* p) {
  parent_->free(p);
}

size_t SlabCache::size() const {
  return parent_->size();
}

}  // namespace __tsan
