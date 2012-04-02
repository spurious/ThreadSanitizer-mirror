//===-- tsan_mman.cc --------------------------------------------*- C++ -*-===//
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
#include "tsan_mman.h"
#include "tsan_allocator.h"
#include "tsan_rtl.h"

namespace __tsan {

void *user_alloc(ThreadState *thr, uptr pc, uptr sz) {
  // May be called from inside rtl.
  CHECK_GT(thr->in_rtl, 0);
  MBlock *b = (MBlock*)Alloc(sz + sizeof(MBlock));
  b->size = sz;
  void *p = b + 1;
  if (thr->in_rtl == 1) {
    MemoryResetRange(thr, pc, (uptr)p, sz);
  }
  return p;
}

void user_free(ThreadState *thr, uptr pc, void *p) {
  CHECK_GT(thr->in_rtl, 0);
  CHECK_NE(p, (void*)0);
  MBlock *b = user_mblock(thr, p);
  if (thr->in_rtl == 1) {
    MemoryRangeFreed(thr, pc, (uptr)p, b->size);
  }
  Free(b);
}

void *user_realloc(ThreadState *thr, uptr pc, void *p, uptr sz) {
  CHECK_GT(thr->in_rtl, 0);
  void *p2 = 0;
  if (sz) {
    p2 = user_alloc(thr, pc, sz);
    if (p) {
      MBlock *b = user_mblock(thr, p);
      internal_memcpy(p2, p, b->size);
    }
  }
  if (p) {
    user_free(thr, pc, p);
  }
  return p2;
}

void *user_alloc_aligned(ThreadState *thr, uptr pc, uptr sz, uptr align) {
  CHECK_GT(thr->in_rtl, 0);
  void *p = user_alloc(thr, pc, sz + align);
  p = (void*)(((uptr)p + align - 1) & ~(align - 1));
  return p;
}

MBlock *user_mblock(ThreadState *thr, void *p) {
  CHECK_GT(thr->in_rtl, 0);
  CHECK_NE(p, (void*)0);
  MBlock *b = (MBlock*)AllocBlock(p);
  // FIXME: Output a warning, it's a user error.
  CHECK_GE(p, (char*)(b + 1));
  CHECK_LE(p, (char*)(b + 1) + b->size);
  return b;
}

void *internal_alloc(ThreadState *thr, uptr sz) {
  CHECK_GT(thr->in_rtl, 0);
  return Alloc(sz);
}

void internal_free(ThreadState *thr, void *p) {
  CHECK_GT(thr->in_rtl, 0);
  Free(p);
}

}  // namespace __tsan
