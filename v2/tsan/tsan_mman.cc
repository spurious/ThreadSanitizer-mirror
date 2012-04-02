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

namespace __tsan {

void *user_alloc(uptr sz) {
  MBlock *b = (MBlock*)Alloc(sz + sizeof(MBlock));
  b->size = sz;
  return b + 1;
}

void user_free(void *p) {
  CHECK_NE(p, (void*)0);
  MBlock *b = (MBlock*)p - 1;
  Free(b);
}

MBlock *user_mblock(void *p) {
  CHECK_NE(p, (void*)0);
  MBlock *b = (MBlock*)AllocBlock(p);
  // FIXME: Output a warning, it's a user error.
  CHECK_GE(p, (char*)(b + 1));
  CHECK_LT(p, (char*)(b + 1) + b->size);
  return b;
}

void *internal_alloc(uptr sz) {
  return Alloc(sz);
}

void internal_free(void *p) {
  Free(p);
}

}  // namespace __tsan
